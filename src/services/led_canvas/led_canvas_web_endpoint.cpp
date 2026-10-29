#include "services/led_canvas/led_canvas_web_endpoint.h"
#include "services/web_access/web_json.h"
#include <limits>

namespace cardputer_hub::services {
namespace {
// Bounded reader for the LED wire format. No recursive JSON tree or traffic-sized allocations.
class Reader {
  public:
    explicit Reader(std::string_view text) : text_(text) {}
    bool token(char c) {
        space();
        if (text_.empty() || text_.front() != c)
            return false;
        text_.remove_prefix(1);
        return true;
    }
    bool done() {
        space();
        return text_.empty();
    }
    bool string(std::string_view& result) {
        if (!token('"'))
            return false;
        const auto end = text_.find('"');
        if (end == std::string_view::npos)
            return false;
        result = text_.substr(0, end);
        for (char c : result)
            if (c < 32 || c == '\\' || static_cast<unsigned char>(c) > 126)
                return false;
        text_.remove_prefix(end + 1);
        return true;
    }
    bool number(std::uint32_t& value) {
        space();
        if (text_.empty() || text_.front() < '0' || text_.front() > '9')
            return false;
        value = 0;
        const bool zero = text_.front() == '0';
        std::size_t digits = 0;
        while (!text_.empty() && text_.front() >= '0' && text_.front() <= '9') {
            const auto digit = static_cast<std::uint32_t>(text_.front() - '0');
            if ((zero && digits) || (value > (UINT32_MAX - digit) / 10))
                return false;
            value = value * 10 + digit;
            ++digits;
            text_.remove_prefix(1);
        }
        return true;
    }
    bool boolean(bool& value) {
        space();
        if (text_.substr(0, 4) == "true") {
            text_.remove_prefix(4);
            value = true;
            return true;
        }
        if (text_.substr(0, 5) == "false") {
            text_.remove_prefix(5);
            value = false;
            return true;
        }
        return false;
    }

  private:
    void space() {
        while (!text_.empty() && (text_.front() == ' ' || text_.front() == '\n' ||
                                  text_.front() == '\r' || text_.front() == '\t'))
            text_.remove_prefix(1);
    }
    std::string_view text_;
};
bool readPoints(Reader& r, LedCanvasBatch& batch) {
    if (!r.token('[') || r.token(']'))
        return false;
    do {
        if (batch.count == batch.points.size() || !r.token('['))
            return false;
        std::array<std::uint32_t, 6> v{};
        for (std::size_t i = 0; i < v.size(); ++i)
            if ((i && !r.token(',')) || !r.number(v[i]))
                return false;
        LedCanvasPoint point;
        if (r.token(',')) {
            std::uint32_t hue = 0, ink = 0;
            if (!r.number(hue) || !r.token(',') || !r.number(ink) ||
                hue >= core::ledCanvasHueUnits || ink > 2)
                return false;
            point.hue = static_cast<std::uint16_t>(hue);
            point.mixInk = static_cast<std::uint8_t>(ink);
            point.hasColor = true;
            if (r.token(',')) {
                std::uint32_t glow = 0;
                if (!r.number(glow) || glow > 1)
                    return false;
                point.glow = glow != 0;
            }
        }
        if (!r.token(']') || v[0] > 4 || v[1] > 2047 || v[2] > 2047 ||
            (v[3] != 0 && (v[3] < 5 || v[3] > 29)) || v[4] > 255 || v[5] > 3)
            return false;
        point.slot = static_cast<std::uint8_t>(v[0]);
        point.x = static_cast<std::uint16_t>(v[1]);
        point.y = static_cast<std::uint16_t>(v[2]);
        point.radius = static_cast<std::uint8_t>(v[3]);
        point.dt = static_cast<std::uint8_t>(v[4]);
        point.flags = static_cast<std::uint8_t>(v[5]);
        batch.points[batch.count++] = point;
    } while (r.token(','));
    return r.token(']');
}
bool readBatch(std::string_view text, LedCanvasBatch& batch) {
    Reader r(text);
    unsigned seen = 0;
    if (!r.token('{'))
        return false;
    do {
        std::string_view key;
        if (!r.string(key) || !r.token(':'))
            return false;
        unsigned bit = 0;
        if (key == "client") {
            bit = 1;
            std::string_view hex;
            if (!r.string(hex) || hex.size() != 8)
                return false;
            for (char c : hex) {
                unsigned digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    return false;
                batch.client = (batch.client << 4) | digit;
            }
        } else if (key == "seq") {
            bit = 2;
            if (!r.number(batch.seq))
                return false;
        } else if (key == "p") {
            bit = 4;
            if (!readPoints(r, batch))
                return false;
        } else
            return false;
        if (seen & bit)
            return false;
        seen |= bit;
    } while (r.token(','));
    return seen == 7 && r.token('}') && r.done();
}
bool readSettings(std::string_view text, LedCanvasSettings& settings) {
    Reader r(text);
    unsigned seen = 0;
    if (!r.token('{'))
        return false;
    do {
        std::string_view key;
        if (!r.string(key) || !r.token(':'))
            return false;
        unsigned bit = 0;
        if (key == "mode" || key == "fade") {
            std::string_view value;
            if (!r.string(value))
                return false;
            bool found = false;
            if (key == "mode") {
                bit = 1;
                for (std::size_t i = 0; i < ledCanvasModeIds.size(); ++i)
                    if (value == ledCanvasModeIds[i]) {
                        settings.mode = static_cast<LedCanvasMode>(i);
                        found = true;
                    }
            } else {
                bit = 2;
                for (std::size_t i = 0; i < ledCanvasFadeIds.size(); ++i)
                    if (value == ledCanvasFadeIds[i]) {
                        settings.fade = static_cast<LedCanvasFade>(i);
                        found = true;
                    }
            }
            if (!found)
                return false;
        } else if (key == "ink") {
            bit = 4;
            std::uint32_t v;
            if (!r.number(v) || v > 6)
                return false;
            settings.ink = static_cast<std::uint8_t>(v);
        } else if (key == "hold") {
            bit = 8;
            if (!r.boolean(settings.hold))
                return false;
        } else
            return false;
        if (seen & bit)
            return false;
        seen |= bit;
    } while (r.token(','));
    return seen == 15 && r.token('}') && r.done();
}
} // namespace
std::string LedCanvasWebEndpoint::summaryJson() {
    const auto s = canvas_.status();
    return std::string("{\"open\":") + (s.open ? "true" : "false") +
           ",\"fingers\":" + std::to_string(s.fingers) + "}";
}
std::string LedCanvasWebEndpoint::statusJson(bool settings) const {
    const auto s = canvas_.status();
    std::string json = std::string("{\"open\":") + (s.open ? "true" : "false") +
                       ",\"revision\":" + std::to_string(s.revision) +
                       ",\"fingers\":" + std::to_string(s.fingers);
    if (settings) {
        json += ",\"settings\":{\"mode\":\"";
        json += ledCanvasModeIds[static_cast<unsigned>(s.settings.mode)];
        json += "\",\"fade\":\"";
        json += ledCanvasFadeIds[static_cast<unsigned>(s.settings.fade)];
        json += "\",\"ink\":" + std::to_string(s.settings.ink) +
                ",\"hold\":" + (s.settings.hold ? "true" : "false") + "}";
    }
    return json + "}";
}
connectivity::HttpResponse LedCanvasWebEndpoint::handle(const connectivity::HttpRequest& request,
                                                        std::string_view path) {
    using connectivity::HttpMethod;
    if (request.method != HttpMethod::Get && request.requestMarker != "1")
        return webErrorResponse(403, "request-marker");
    if (request.body.size() > connectivity::httpMaxRequestBodyBytes)
        return webErrorResponse(413, "body-too-large");
    if (path.empty())
        return request.method == HttpMethod::Get ? webJsonResponse(200, statusJson(true))
                                                 : webErrorResponse(405, "method");
    if (path == "/settings") {
        if (request.method != HttpMethod::Put)
            return webErrorResponse(405, "method");
        LedCanvasSettings settings;
        if (!readSettings(request.body, settings) || !canvas_.setSettings(settings))
            return webErrorResponse(400, "settings");
        return webJsonResponse(200, statusJson(true));
    }
    if (path == "/strokes" || path == "/clear") {
        if (request.method != HttpMethod::Post)
            return webErrorResponse(405, "method");
        if (!canvas_.status().open)
            return webErrorResponse(409, "app-closed");
        if (path == "/clear") {
            canvas_.clear();
            return webEmptyResponse(204);
        }
        LedCanvasBatch batch;
        if (!readBatch(request.body, batch) || !canvas_.submit(batch))
            return webErrorResponse(400, "strokes");
        return webJsonResponse(200, statusJson(false));
    }
    return webErrorResponse(404, "not-found");
}
} // namespace cardputer_hub::services
