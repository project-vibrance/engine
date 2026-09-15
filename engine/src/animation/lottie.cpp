#include "lottie.h"

#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using Json = simdjson::dom::element;

    constexpr double kPi = 3.14159265358979323846;

    bool json_field(Json object, std::string_view name, Json& value)
    {
        simdjson::dom::object parsedObject;
        if (object.get_object().get(parsedObject) != simdjson::SUCCESS)
        {
            return false;
        }
        return parsedObject.at_key(name).get(value) == simdjson::SUCCESS;
    }

    std::optional<double> json_number(Json value)
    {
        double number = 0.0;
        if (value.get_double().get(number) == simdjson::SUCCESS)
        {
            return number;
        }
        return std::nullopt;
    }

    std::optional<double> json_number(Json object, std::string_view name)
    {
        Json value;
        return json_field(object, name, value) ? json_number(value) : std::nullopt;
    }

    bool json_bool(Json object, std::string_view name, bool fallback = false)
    {
        Json value;
        if (!json_field(object, name, value))
        {
            return fallback;
        }
        bool result = false;
        if (value.get_bool().get(result) == simdjson::SUCCESS)
        {
            return result;
        }
        if (const std::optional<double> numeric = json_number(value))
        {
            return *numeric != 0.0;
        }
        return fallback;
    }

    std::string json_string(Json object, std::string_view name)
    {
        Json value;
        if (!json_field(object, name, value))
        {
            return {};
        }
        std::string_view text;
        return value.get_string().get(text) == simdjson::SUCCESS
            ? std::string(text)
            : std::string();
    }

    std::optional<simdjson::dom::array> json_array(Json object, std::string_view name)
    {
        Json value;
        if (!json_field(object, name, value))
        {
            return std::nullopt;
        }
        simdjson::dom::array array;
        if (value.get_array().get(array) != simdjson::SUCCESS)
        {
            return std::nullopt;
        }
        return array;
    }

    template <std::size_t N>
    using Value = std::array<double, N>;

    template <std::size_t N>
    bool read_value(Json value, Value<N>& result)
    {
        if (const std::optional<double> number = json_number(value))
        {
            result[0] = *number;
            return true;
        }

        simdjson::dom::array array;
        if (value.get_array().get(array) != simdjson::SUCCESS)
        {
            return false;
        }

        std::size_t index = 0u;
        for (Json component : array)
        {
            if (index >= N)
            {
                break;
            }
            const std::optional<double> number = json_number(component);
            if (!number)
            {
                return false;
            }
            result[index++] = *number;
        }
        return index > 0u;
    }

    double first_number(Json value, double fallback)
    {
        if (const std::optional<double> number = json_number(value))
        {
            return *number;
        }
        simdjson::dom::array array;
        if (value.get_array().get(array) == simdjson::SUCCESS)
        {
            for (Json component : array)
            {
                if (const std::optional<double> number = json_number(component))
                {
                    return *number;
                }
                break;
            }
        }
        return fallback;
    }

    struct TemporalEase
    {
        double outX = 0.3333333333;
        double outY = 0.0;
        double inX = 0.6666666667;
        double inY = 1.0;
    };

    template <std::size_t N>
    struct Keyframe
    {
        double time = 0.0;
        Value<N> value {};
        TemporalEase ease {};
        bool hold = false;
    };

    template <std::size_t N>
    struct AnimatedValue
    {
        Value<N> value {};
        std::vector<Keyframe<N>> keyframes;

        bool animated() const
        {
            return keyframes.size() > 1u;
        }
    };

    TemporalEase parse_temporal_ease(Json keyframe)
    {
        TemporalEase ease;
        Json control;
        Json component;
        if (json_field(keyframe, "o", control))
        {
            if (json_field(control, "x", component))
            {
                ease.outX = first_number(component, ease.outX);
            }
            if (json_field(control, "y", component))
            {
                ease.outY = first_number(component, ease.outY);
            }
        }
        if (json_field(keyframe, "i", control))
        {
            if (json_field(control, "x", component))
            {
                ease.inX = first_number(component, ease.inX);
            }
            if (json_field(control, "y", component))
            {
                ease.inY = first_number(component, ease.inY);
            }
        }
        ease.outX = std::clamp(ease.outX, 0.0, 1.0);
        ease.inX = std::clamp(ease.inX, 0.0, 1.0);
        return ease;
    }

    template <std::size_t N>
    AnimatedValue<N> parse_animated_value(
        Json property,
        Value<N> fallback,
        uint32_t& ignoredExpressions,
        uint32_t& unsupportedFeatures)
    {
        AnimatedValue<N> result;
        result.value = fallback;

        Json expression;
        if (json_field(property, "x", expression))
        {
            std::string_view source;
            if (expression.get_string().get(source) == simdjson::SUCCESS && !source.empty())
            {
                ++ignoredExpressions;
            }
        }

        Json keyData;
        if (!json_field(property, "k", keyData))
        {
            return result;
        }

        const bool markedAnimated = json_number(property, "a").value_or(0.0) != 0.0;
        simdjson::dom::array array;
        if (!markedAnimated || keyData.get_array().get(array) != simdjson::SUCCESS)
        {
            read_value(keyData, result.value);
            return result;
        }

        for (Json item : array)
        {
            simdjson::dom::object object;
            if (item.get_object().get(object) != simdjson::SUCCESS)
            {
                // Some exporters mark a numeric vector as animated even when
                // it contains no keyframes. Treat it as a static value.
                read_value(keyData, result.value);
                return result;
            }

            Keyframe<N> keyframe;
            keyframe.value = result.keyframes.empty()
                ? result.value
                : result.keyframes.back().value;
            keyframe.time = json_number(item, "t").value_or(0.0);
            keyframe.hold = json_number(item, "h").value_or(0.0) != 0.0;
            keyframe.ease = parse_temporal_ease(item);

            Json startValue;
            if (json_field(item, "s", startValue))
            {
                read_value(startValue, keyframe.value);
            }
            else if (Json endValue; json_field(item, "e", endValue))
            {
                read_value(endValue, keyframe.value);
            }
            result.keyframes.push_back(keyframe);
        }

        if (!result.keyframes.empty())
        {
            result.value = result.keyframes.front().value;
        }
        if (result.keyframes.size() == 1u)
        {
            ++unsupportedFeatures;
        }
        return result;
    }

    double cubic_coordinate(double t, double first, double second)
    {
        const double oneMinusT = 1.0 - t;
        return 3.0 * oneMinusT * oneMinusT * t * first +
            3.0 * oneMinusT * t * t * second +
            t * t * t;
    }

    double cubic_derivative(double t, double first, double second)
    {
        const double oneMinusT = 1.0 - t;
        return 3.0 * oneMinusT * oneMinusT * first +
            6.0 * oneMinusT * t * (second - first) +
            3.0 * t * t * (1.0 - second);
    }

    double cubic_bezier_progress(double progress, const TemporalEase& ease)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        double parameter = progress;
        for (int iteration = 0; iteration < 6; ++iteration)
        {
            const double error = cubic_coordinate(parameter, ease.outX, ease.inX) - progress;
            const double derivative = cubic_derivative(parameter, ease.outX, ease.inX);
            if (std::abs(error) < 0.00001 || std::abs(derivative) < 0.000001)
            {
                break;
            }
            parameter = std::clamp(parameter - error / derivative, 0.0, 1.0);
        }

        double low = 0.0;
        double high = 1.0;
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            const double x = cubic_coordinate(parameter, ease.outX, ease.inX);
            if (std::abs(x - progress) < 0.00001)
            {
                break;
            }
            if (x < progress)
            {
                low = parameter;
            }
            else
            {
                high = parameter;
            }
            parameter = (low + high) * 0.5;
        }
        return cubic_coordinate(parameter, ease.outY, ease.inY);
    }

    template <std::size_t N>
    Value<N> evaluate(const AnimatedValue<N>& property, double frame)
    {
        if (property.keyframes.empty() || frame <= property.keyframes.front().time)
        {
            return property.value;
        }
        if (frame >= property.keyframes.back().time)
        {
            return property.keyframes.back().value;
        }

        for (std::size_t index = 0u; index + 1u < property.keyframes.size(); ++index)
        {
            const Keyframe<N>& current = property.keyframes[index];
            const Keyframe<N>& next = property.keyframes[index + 1u];
            if (frame > next.time)
            {
                continue;
            }

            const double duration = std::max(next.time - current.time, 0.000001);
            const double linearProgress = std::clamp((frame - current.time) / duration, 0.0, 1.0);
            const double progress = current.hold
                ? 0.0
                : cubic_bezier_progress(linearProgress, current.ease);
            Value<N> value {};
            for (std::size_t component = 0u; component < N; ++component)
            {
                value[component] = current.value[component] +
                    (next.value[component] - current.value[component]) * progress;
            }
            return value;
        }
        return property.keyframes.back().value;
    }

    struct Matrix
    {
        double a = 1.0;
        double b = 0.0;
        double c = 0.0;
        double d = 1.0;
        double e = 0.0;
        double f = 0.0;
    };

    Matrix operator*(const Matrix& left, const Matrix& right)
    {
        return {
            left.a * right.a + left.c * right.b,
            left.b * right.a + left.d * right.b,
            left.a * right.c + left.c * right.d,
            left.b * right.c + left.d * right.d,
            left.a * right.e + left.c * right.f + left.e,
            left.b * right.e + left.d * right.f + left.f
        };
    }

    Matrix translation(double x, double y)
    {
        return { 1.0, 0.0, 0.0, 1.0, x, y };
    }

    Matrix rotation(double degrees)
    {
        const double radians = degrees * kPi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return { cosine, sine, -sine, cosine, 0.0, 0.0 };
    }

    Matrix scaling(double x, double y)
    {
        return { x, 0.0, 0.0, y, 0.0, 0.0 };
    }

    struct Transform
    {
        AnimatedValue<3> position;
        std::optional<AnimatedValue<1>> separatedPositionX;
        std::optional<AnimatedValue<1>> separatedPositionY;
        std::optional<AnimatedValue<1>> separatedPositionZ;
        AnimatedValue<3> anchor;
        AnimatedValue<3> scale;
        AnimatedValue<1> rotation;
        AnimatedValue<1> opacity;
    };

    Transform identity_transform()
    {
        Transform result;
        result.position.value = { 0.0, 0.0, 0.0 };
        result.anchor.value = { 0.0, 0.0, 0.0 };
        result.scale.value = { 100.0, 100.0, 100.0 };
        result.rotation.value = { 0.0 };
        result.opacity.value = { 100.0 };
        return result;
    }

    Transform parse_transform(
        Json transform,
        uint32_t& ignoredExpressions,
        uint32_t& unsupportedFeatures)
    {
        Transform result = identity_transform();

        const auto parseProperty = [&](std::string_view name, auto fallback) {
            Json property;
            using PropertyType = decltype(parse_animated_value(
                property,
                fallback,
                ignoredExpressions,
                unsupportedFeatures));
            return json_field(transform, name, property)
                ? parse_animated_value(
                    property,
                    fallback,
                    ignoredExpressions,
                    unsupportedFeatures)
                : PropertyType { fallback, {} };
        };

        Json positionProperty;
        if (json_field(transform, "p", positionProperty))
        {
            if (json_bool(positionProperty, "s"))
            {
                const auto parseSeparatedDimension = [&](std::string_view name) {
                    Json dimension;
                    return json_field(positionProperty, name, dimension)
                        ? std::optional<AnimatedValue<1>>(parse_animated_value(
                            dimension,
                            Value<1> { 0.0 },
                            ignoredExpressions,
                            unsupportedFeatures))
                        : std::optional<AnimatedValue<1>> {};
                };

                result.separatedPositionX = parseSeparatedDimension("x");
                result.separatedPositionY = parseSeparatedDimension("y");
                result.separatedPositionZ = parseSeparatedDimension("z");
                if (!result.separatedPositionX || !result.separatedPositionY)
                {
                    ++unsupportedFeatures;
                }
            }
            else
            {
                result.position = parse_animated_value(
                    positionProperty,
                    Value<3> { 0.0, 0.0, 0.0 },
                    ignoredExpressions,
                    unsupportedFeatures);
            }
        }
        result.anchor = parseProperty("a", Value<3> { 0.0, 0.0, 0.0 });
        result.scale = parseProperty("s", Value<3> { 100.0, 100.0, 100.0 });
        result.rotation = parseProperty("r", Value<1> { 0.0 });
        result.opacity = parseProperty("o", Value<1> { 100.0 });
        return result;
    }

    Matrix evaluate_transform(const Transform& transform, double frame)
    {
        Value<3> position = evaluate(transform.position, frame);
        if (transform.separatedPositionX)
        {
            position[0] = evaluate(*transform.separatedPositionX, frame)[0];
        }
        if (transform.separatedPositionY)
        {
            position[1] = evaluate(*transform.separatedPositionY, frame)[0];
        }
        if (transform.separatedPositionZ)
        {
            position[2] = evaluate(*transform.separatedPositionZ, frame)[0];
        }
        const Value<3> anchor = evaluate(transform.anchor, frame);
        const Value<3> scale = evaluate(transform.scale, frame);
        const Value<1> angle = evaluate(transform.rotation, frame);
        return translation(position[0], position[1]) *
            rotation(angle[0]) *
            scaling(scale[0] * 0.01, scale[1] * 0.01) *
            translation(-anchor[0], -anchor[1]);
    }

    double evaluate_opacity(const Transform& transform, double frame)
    {
        return std::clamp(evaluate(transform.opacity, frame)[0] * 0.01, 0.0, 1.0);
    }

    struct Path
    {
        std::vector<Value<2>> vertices;
        std::vector<Value<2>> inTangents;
        std::vector<Value<2>> outTangents;
        bool closed = false;

        bool valid() const
        {
            return !vertices.empty() &&
                vertices.size() == inTangents.size() &&
                vertices.size() == outTangents.size();
        }
    };

    std::vector<Value<2>> parse_vec2_array(Json object, std::string_view name)
    {
        std::vector<Value<2>> values;
        const std::optional<simdjson::dom::array> array = json_array(object, name);
        if (!array)
        {
            return values;
        }
        for (Json item : *array)
        {
            Value<2> value {};
            if (read_value(item, value))
            {
                values.push_back(value);
            }
        }
        return values;
    }

    Path parse_path(Json shape, uint32_t& unsupportedFeatures)
    {
        Path path;
        Json property;
        Json value;
        if (!json_field(shape, "ks", property) || !json_field(property, "k", value))
        {
            ++unsupportedFeatures;
            return path;
        }

        if (json_number(property, "a").value_or(0.0) != 0.0)
        {
            // Animated path morphing is intentionally deferred. The first
            // shape remains a deterministic fallback instead of disappearing.
            simdjson::dom::array keyframes;
            if (value.get_array().get(keyframes) == simdjson::SUCCESS)
            {
                for (Json keyframe : keyframes)
                {
                    Json start;
                    simdjson::dom::array startArray;
                    if (json_field(keyframe, "s", start) &&
                        start.get_array().get(startArray) == simdjson::SUCCESS)
                    {
                        for (Json candidate : startArray)
                        {
                            value = candidate;
                            break;
                        }
                    }
                    break;
                }
            }
            ++unsupportedFeatures;
        }

        path.vertices = parse_vec2_array(value, "v");
        path.inTangents = parse_vec2_array(value, "i");
        path.outTangents = parse_vec2_array(value, "o");
        path.closed = json_bool(value, "c");
        if (!path.valid())
        {
            ++unsupportedFeatures;
        }
        return path;
    }

    struct Paint
    {
        AnimatedValue<4> color;
        AnimatedValue<1> opacity;
        AnimatedValue<1> width;
        bool present = false;
        int lineCap = 1;
        int lineJoin = 1;
    };

    struct TrimPaths
    {
        AnimatedValue<1> start;
        AnimatedValue<1> end;
        AnimatedValue<1> offset;
        bool present = false;
        int mode = 1;
    };

    struct ShapeGroup
    {
        std::vector<Path> paths;
        Transform transform;
        Paint fill;
        Paint stroke;
        TrimPaths trim;
        bool evenOddFill = false;
        bool hidden = false;
    };

    Paint parse_paint(
        Json item,
        bool stroke,
        uint32_t& ignoredExpressions,
        uint32_t& unsupportedFeatures)
    {
        Paint paint;
        paint.present = true;
        paint.color.value = { 0.0, 0.0, 0.0, 1.0 };
        paint.opacity.value = { 100.0 };
        paint.width.value = { 1.0 };

        Json property;
        if (json_field(item, "c", property))
        {
            paint.color = parse_animated_value(
                property,
                Value<4> { 0.0, 0.0, 0.0, 1.0 },
                ignoredExpressions,
                unsupportedFeatures);
        }
        if (json_field(item, "o", property))
        {
            paint.opacity = parse_animated_value(
                property,
                Value<1> { 100.0 },
                ignoredExpressions,
                unsupportedFeatures);
        }
        if (stroke && json_field(item, "w", property))
        {
            paint.width = parse_animated_value(
                property,
                Value<1> { 1.0 },
                ignoredExpressions,
                unsupportedFeatures);
        }
        paint.lineCap = static_cast<int>(json_number(item, "lc").value_or(1.0));
        paint.lineJoin = static_cast<int>(json_number(item, "lj").value_or(1.0));
        return paint;
    }

    TrimPaths parse_trim_paths(
        Json item,
        uint32_t& ignoredExpressions,
        uint32_t& unsupportedFeatures)
    {
        TrimPaths trim;
        trim.present = !json_bool(item, "hd");
        trim.start.value = { 0.0 };
        trim.end.value = { 100.0 };
        trim.offset.value = { 0.0 };
        trim.mode = static_cast<int>(json_number(item, "m").value_or(1.0));

        const auto parseProperty = [&](std::string_view name, Value<1> fallback) {
            Json property;
            return json_field(item, name, property)
                ? parse_animated_value(
                    property,
                    fallback,
                    ignoredExpressions,
                    unsupportedFeatures)
                : AnimatedValue<1> { fallback, {} };
        };

        trim.start = parseProperty("s", Value<1> { 0.0 });
        trim.end = parseProperty("e", Value<1> { 100.0 });
        trim.offset = parseProperty("o", Value<1> { 0.0 });
        return trim;
    }

    ShapeGroup parse_shape_group(
        Json group,
        uint32_t& ignoredExpressions,
        uint32_t& unsupportedFeatures)
    {
        ShapeGroup result;
        result.hidden = json_bool(group, "hd");
        result.transform = identity_transform();

        const std::optional<simdjson::dom::array> items = json_array(group, "it");
        if (!items)
        {
            ++unsupportedFeatures;
            return result;
        }

        for (Json item : *items)
        {
            const std::string type = json_string(item, "ty");
            if (type == "sh")
            {
                Path path = parse_path(item, unsupportedFeatures);
                if (path.valid())
                {
                    result.paths.push_back(std::move(path));
                }
            }
            else if (type == "fl")
            {
                result.fill = parse_paint(
                    item,
                    false,
                    ignoredExpressions,
                    unsupportedFeatures);
                result.evenOddFill =
                    static_cast<int>(json_number(item, "r").value_or(1.0)) == 2;
            }
            else if (type == "st")
            {
                result.stroke = parse_paint(
                    item,
                    true,
                    ignoredExpressions,
                    unsupportedFeatures);
            }
            else if (type == "tm")
            {
                result.trim = parse_trim_paths(
                    item,
                    ignoredExpressions,
                    unsupportedFeatures);
            }
            else if (type == "tr")
            {
                result.transform = parse_transform(
                    item,
                    ignoredExpressions,
                    unsupportedFeatures);
            }
            else if (type == "gr")
            {
                // Nested groups require a retained shape tree. Keep counting
                // them explicitly rather than silently rendering them wrong.
                ++unsupportedFeatures;
            }
            else if (type != "no" && !type.empty())
            {
                ++unsupportedFeatures;
            }
        }
        if (result.trim.present && result.trim.mode == 1 && result.paths.size() > 1u)
        {
            // Simultaneous trim treats every path as one continuous contour.
            // The common one-path form is exact; multi-path groups currently
            // use the deterministic per-path fallback below.
            ++unsupportedFeatures;
        }
        return result;
    }

    struct Layer
    {
        int index = 0;
        int parent = 0;
        int type = 0;
        double inFrame = 0.0;
        double outFrame = 0.0;
        bool hidden = false;
        Transform transform;
        std::vector<ShapeGroup> groups;
    };

    struct Document
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        double frameRate = 0.0;
        double inFrame = 0.0;
        double outFrame = 0.0;
        std::vector<Layer> layers;
        std::unordered_map<int, std::size_t> layerByIndex;
        uint32_t ignoredExpressionCount = 0u;
        uint32_t unsupportedFeatureCount = 0u;
    };

    bool parse_document(Json root, Document& document, std::string& error)
    {
        document.width = static_cast<uint32_t>(
            std::max(json_number(root, "w").value_or(0.0), 0.0));
        document.height = static_cast<uint32_t>(
            std::max(json_number(root, "h").value_or(0.0), 0.0));
        document.frameRate = json_number(root, "fr").value_or(0.0);
        document.inFrame = json_number(root, "ip").value_or(0.0);
        document.outFrame = json_number(root, "op").value_or(0.0);
        if (document.width == 0u || document.height == 0u ||
            document.frameRate <= 0.0 || document.outFrame <= document.inFrame)
        {
            error = "Lottie composition has invalid dimensions or timeline metadata.";
            return false;
        }

        const std::optional<simdjson::dom::array> layers = json_array(root, "layers");
        if (!layers)
        {
            error = "Lottie composition has no layers array.";
            return false;
        }

        for (Json layerJson : *layers)
        {
            Layer layer;
            layer.index = static_cast<int>(json_number(layerJson, "ind").value_or(0.0));
            layer.parent = static_cast<int>(json_number(layerJson, "parent").value_or(0.0));
            layer.type = static_cast<int>(json_number(layerJson, "ty").value_or(-1.0));
            layer.inFrame = json_number(layerJson, "ip").value_or(document.inFrame);
            layer.outFrame = json_number(layerJson, "op").value_or(document.outFrame);
            layer.hidden = json_bool(layerJson, "hd");

            Json transform;
            if (json_field(layerJson, "ks", transform))
            {
                layer.transform = parse_transform(
                    transform,
                    document.ignoredExpressionCount,
                    document.unsupportedFeatureCount);
            }
            else
            {
                ++document.unsupportedFeatureCount;
            }

            if (layer.type == 4)
            {
                const std::optional<simdjson::dom::array> shapes =
                    json_array(layerJson, "shapes");
                if (shapes)
                {
                    for (Json shape : *shapes)
                    {
                        if (json_string(shape, "ty") == "gr")
                        {
                            layer.groups.push_back(parse_shape_group(
                                shape,
                                document.ignoredExpressionCount,
                                document.unsupportedFeatureCount));
                        }
                        else
                        {
                            ++document.unsupportedFeatureCount;
                        }
                    }
                }
            }
            else if (layer.type != 3)
            {
                ++document.unsupportedFeatureCount;
            }

            document.layerByIndex[layer.index] = document.layers.size();
            document.layers.push_back(std::move(layer));
        }
        return true;
    }

    Matrix layer_matrix(
        const Document& document,
        std::size_t layerIndex,
        double frame,
        uint32_t depth = 0u)
    {
        if (depth > document.layers.size())
        {
            return {};
        }
        const Layer& layer = document.layers[layerIndex];
        const Matrix local = evaluate_transform(layer.transform, frame);
        const auto parent = document.layerByIndex.find(layer.parent);
        if (layer.parent == 0 || parent == document.layerByIndex.end() ||
            parent->second == layerIndex)
        {
            return local;
        }
        return layer_matrix(document, parent->second, frame, depth + 1u) * local;
    }

    double layer_opacity(
        const Document& document,
        std::size_t layerIndex,
        double frame,
        uint32_t depth = 0u)
    {
        if (depth > document.layers.size())
        {
            return 1.0;
        }
        const Layer& layer = document.layers[layerIndex];
        const double local = evaluate_opacity(layer.transform, frame);
        const auto parent = document.layerByIndex.find(layer.parent);
        if (layer.parent == 0 || parent == document.layerByIndex.end() ||
            parent->second == layerIndex)
        {
            return local;
        }
        return local * layer_opacity(document, parent->second, frame, depth + 1u);
    }

    void append_matrix(std::ostringstream& svg, const Matrix& matrix)
    {
        svg << "matrix(" << matrix.a << ' ' << matrix.b << ' '
            << matrix.c << ' ' << matrix.d << ' '
            << matrix.e << ' ' << matrix.f << ')';
    }

    std::string svg_color(const Value<4>& color)
    {
        const int red = static_cast<int>(std::round(std::clamp(color[0], 0.0, 1.0) * 255.0));
        const int green = static_cast<int>(std::round(std::clamp(color[1], 0.0, 1.0) * 255.0));
        const int blue = static_cast<int>(std::round(std::clamp(color[2], 0.0, 1.0) * 255.0));
        return "rgb(" + std::to_string(red) + "," + std::to_string(green) +
            "," + std::to_string(blue) + ")";
    }

    const char* svg_line_cap(int lineCap)
    {
        return lineCap == 2 ? "round" : lineCap == 3 ? "square" : "butt";
    }

    const char* svg_line_join(int lineJoin)
    {
        return lineJoin == 2 ? "round" : lineJoin == 3 ? "bevel" : "miter";
    }

    void append_path_data(std::ostringstream& svg, const Path& path)
    {
        if (!path.valid())
        {
            return;
        }
        svg << 'M' << path.vertices[0][0] << ' ' << path.vertices[0][1];
        const std::size_t segmentCount = path.closed
            ? path.vertices.size()
            : path.vertices.size() - 1u;
        for (std::size_t segment = 0u; segment < segmentCount; ++segment)
        {
            const std::size_t next = (segment + 1u) % path.vertices.size();
            svg << " C"
                << path.vertices[segment][0] + path.outTangents[segment][0] << ' '
                << path.vertices[segment][1] + path.outTangents[segment][1] << ' '
                << path.vertices[next][0] + path.inTangents[next][0] << ' '
                << path.vertices[next][1] + path.inTangents[next][1] << ' '
                << path.vertices[next][0] << ' ' << path.vertices[next][1];
        }
        if (path.closed)
        {
            svg << 'Z';
        }
    }

    struct EvaluatedTrimPaths
    {
        double start = 0.0;
        double extent = 1.0;
        bool present = false;
    };

    EvaluatedTrimPaths evaluate_trim_paths(const TrimPaths& trim, double frame)
    {
        if (!trim.present)
        {
            return {};
        }

        const double rawStart = evaluate(trim.start, frame)[0] * 0.01;
        const double rawEnd = evaluate(trim.end, frame)[0] * 0.01;
        const double rawExtent = rawEnd - rawStart;
        if (std::abs(rawExtent) >= 1.0 - 0.000001)
        {
            return { 0.0, 1.0, true };
        }

        const double offset = evaluate(trim.offset, frame)[0] / 360.0;
        double start = std::fmod(rawStart + offset, 1.0);
        if (start < 0.0)
        {
            start += 1.0;
        }
        double extent = std::fmod(rawExtent, 1.0);
        if (extent < 0.0)
        {
            extent += 1.0;
        }
        return { start, std::clamp(extent, 0.0, 1.0), true };
    }

    Value<2> cubic_point(
        const Value<2>& start,
        const Value<2>& control1,
        const Value<2>& control2,
        const Value<2>& end,
        double progress)
    {
        const double inverse = 1.0 - progress;
        const double startWeight = inverse * inverse * inverse;
        const double control1Weight = 3.0 * inverse * inverse * progress;
        const double control2Weight = 3.0 * inverse * progress * progress;
        const double endWeight = progress * progress * progress;
        return {
            start[0] * startWeight + control1[0] * control1Weight +
                control2[0] * control2Weight + end[0] * endWeight,
            start[1] * startWeight + control1[1] * control1Weight +
                control2[1] * control2Weight + end[1] * endWeight
        };
    }

    Value<2> interpolate_point(
        const Value<2>& start,
        const Value<2>& end,
        double progress)
    {
        return {
            start[0] + (end[0] - start[0]) * progress,
            start[1] + (end[1] - start[1]) * progress
        };
    }

    struct CubicCurve
    {
        Value<2> start {};
        Value<2> control1 {};
        Value<2> control2 {};
        Value<2> end {};
    };

    CubicCurve path_cubic(const Path& path, std::size_t segment)
    {
        const std::size_t next = (segment + 1u) % path.vertices.size();
        return {
            path.vertices[segment],
            {
                path.vertices[segment][0] + path.outTangents[segment][0],
                path.vertices[segment][1] + path.outTangents[segment][1]
            },
            {
                path.vertices[next][0] + path.inTangents[next][0],
                path.vertices[next][1] + path.inTangents[next][1]
            },
            path.vertices[next]
        };
    }

    std::array<CubicCurve, 2> split_cubic(
        const CubicCurve& curve,
        double progress)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        const Value<2> first = interpolate_point(
            curve.start,
            curve.control1,
            progress);
        const Value<2> second = interpolate_point(
            curve.control1,
            curve.control2,
            progress);
        const Value<2> third = interpolate_point(
            curve.control2,
            curve.end,
            progress);
        const Value<2> fourth = interpolate_point(first, second, progress);
        const Value<2> fifth = interpolate_point(second, third, progress);
        const Value<2> split = interpolate_point(fourth, fifth, progress);
        return {
            CubicCurve { curve.start, first, fourth, split },
            CubicCurve { split, fifth, third, curve.end }
        };
    }

    CubicCurve cubic_interval(
        const CubicCurve& curve,
        double start,
        double end)
    {
        start = std::clamp(start, 0.0, 1.0);
        end = std::clamp(end, start, 1.0);
        CubicCurve result = end < 1.0
            ? split_cubic(curve, end)[0]
            : curve;
        if (start > 0.0 && end > 0.0)
        {
            result = split_cubic(result, start / end)[1];
        }
        return result;
    }

    constexpr uint32_t kCubicLengthSamples = 48u;

    struct MeasuredCubic
    {
        CubicCurve curve;
        std::array<double, kCubicLengthSamples + 1u> cumulativeLength {};
        double length = 0.0;
    };

    MeasuredCubic measure_cubic(const CubicCurve& curve)
    {
        MeasuredCubic measured;
        measured.curve = curve;
        Value<2> previous = curve.start;
        for (uint32_t sample = 1u; sample <= kCubicLengthSamples; ++sample)
        {
            const double progress = static_cast<double>(sample) /
                static_cast<double>(kCubicLengthSamples);
            const Value<2> current = cubic_point(
                curve.start,
                curve.control1,
                curve.control2,
                curve.end,
                progress);
            const double deltaX = current[0] - previous[0];
            const double deltaY = current[1] - previous[1];
            measured.length += std::sqrt(deltaX * deltaX + deltaY * deltaY);
            measured.cumulativeLength[sample] = measured.length;
            previous = current;
        }
        return measured;
    }

    std::vector<MeasuredCubic> measure_path(const Path& path)
    {
        std::vector<MeasuredCubic> measured;
        if (!path.valid() || (!path.closed && path.vertices.size() < 2u))
        {
            return measured;
        }
        const std::size_t segmentCount = path.closed
            ? path.vertices.size()
            : path.vertices.size() - 1u;
        measured.reserve(segmentCount);
        for (std::size_t segment = 0u; segment < segmentCount; ++segment)
        {
            measured.push_back(measure_cubic(path_cubic(path, segment)));
        }
        return measured;
    }

    double cubic_parameter_at_length(
        const MeasuredCubic& curve,
        double distance)
    {
        if (curve.length <= 0.000001)
        {
            return 0.0;
        }
        distance = std::clamp(distance, 0.0, curve.length);
        for (uint32_t sample = 1u; sample <= kCubicLengthSamples; ++sample)
        {
            if (distance > curve.cumulativeLength[sample])
            {
                continue;
            }
            const double previousLength = curve.cumulativeLength[sample - 1u];
            const double sampleLength = std::max(
                curve.cumulativeLength[sample] - previousLength,
                0.000001);
            const double withinSample = std::clamp(
                (distance - previousLength) / sampleLength,
                0.0,
                1.0);
            return (static_cast<double>(sample - 1u) + withinSample) /
                static_cast<double>(kCubicLengthSamples);
        }
        return 1.0;
    }

    void append_trimmed_interval(
        std::ostringstream& svg,
        const std::vector<MeasuredCubic>& curves,
        double intervalStart,
        double intervalEnd)
    {
        if (intervalEnd - intervalStart <= 0.000001)
        {
            return;
        }

        double curveStart = 0.0;
        bool beganSubpath = false;
        for (const MeasuredCubic& measured : curves)
        {
            const double curveEnd = curveStart + measured.length;
            const double overlapStart = std::max(intervalStart, curveStart);
            const double overlapEnd = std::min(intervalEnd, curveEnd);
            if (overlapEnd - overlapStart > 0.000001 &&
                measured.length > 0.000001)
            {
                const double localStart = overlapStart - curveStart;
                const double localEnd = overlapEnd - curveStart;
                const CubicCurve visible = cubic_interval(
                    measured.curve,
                    cubic_parameter_at_length(measured, localStart),
                    cubic_parameter_at_length(measured, localEnd));
                if (!beganSubpath)
                {
                    svg << 'M' << visible.start[0] << ' ' << visible.start[1];
                    beganSubpath = true;
                }
                svg << " C"
                    << visible.control1[0] << ' ' << visible.control1[1] << ' '
                    << visible.control2[0] << ' ' << visible.control2[1] << ' '
                    << visible.end[0] << ' ' << visible.end[1];
            }
            curveStart = curveEnd;
            if (curveStart >= intervalEnd)
            {
                break;
            }
        }
    }

    void append_trimmed_path_data(
        std::ostringstream& svg,
        const Path& path,
        const EvaluatedTrimPaths& trim)
    {
        if (!trim.present || trim.extent >= 1.0 - 0.000001)
        {
            append_path_data(svg, path);
            return;
        }
        if (trim.extent <= 0.000001)
        {
            return;
        }

        const std::vector<MeasuredCubic> curves = measure_path(path);
        double pathLength = 0.0;
        for (const MeasuredCubic& curve : curves)
        {
            pathLength += curve.length;
        }
        if (pathLength <= 0.000001)
        {
            return;
        }

        const double start = trim.start * pathLength;
        const double visibleLength = trim.extent * pathLength;
        const double firstEnd = std::min(start + visibleLength, pathLength);
        append_trimmed_interval(svg, curves, start, firstEnd);

        const double wrappedLength = visibleLength - (firstEnd - start);
        if (wrappedLength > 0.000001)
        {
            append_trimmed_interval(
                svg,
                curves,
                0.0,
                std::min(wrappedLength, pathLength));
        }
    }

    std::string render_svg_frame(const Document& document, double frame)
    {
        std::ostringstream svg;
        svg << std::fixed << std::setprecision(5);
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
            << document.width << "\" height=\"" << document.height
            << "\" viewBox=\"0 0 " << document.width << ' ' << document.height
            << "\" preserveAspectRatio=\"xMidYMid meet\">";

        // Lottie stores layers from front to back; SVG paints back to front.
        for (std::size_t reverse = document.layers.size(); reverse > 0u; --reverse)
        {
            const std::size_t layerIndex = reverse - 1u;
            const Layer& layer = document.layers[layerIndex];
            if (layer.type != 4 || layer.hidden ||
                frame < layer.inFrame || frame >= layer.outFrame)
            {
                continue;
            }

            const Matrix layerTransform = layer_matrix(document, layerIndex, frame);
            const double layerOpacity = layer_opacity(document, layerIndex, frame);
            svg << "<g transform=\"";
            append_matrix(svg, layerTransform);
            svg << "\">";

            for (const ShapeGroup& group : layer.groups)
            {
                if (group.hidden || group.paths.empty())
                {
                    continue;
                }
                svg << "<g transform=\"";
                append_matrix(svg, evaluate_transform(group.transform, frame));
                svg << "\">";

                // NanoSVG stores one opacity value on a shape. A nested group
                // opacity replaces its parent's value instead of multiplying
                // it, which makes animated Lottie layer fades disappear when
                // the inner shape transform has its usual 100% opacity. Fold
                // the complete inherited opacity chain into both paints so
                // layer, parent-layer, group, fill and stroke fades survive
                // rasterisation.
                const double inheritedOpacity = std::clamp(
                    layerOpacity * evaluate_opacity(group.transform, frame),
                    0.0,
                    1.0);

                const Value<4> fillColor = evaluate(group.fill.color, frame);
                const Value<1> fillOpacity = evaluate(group.fill.opacity, frame);
                const Value<4> strokeColor = evaluate(group.stroke.color, frame);
                const Value<1> strokeOpacity = evaluate(group.stroke.opacity, frame);
                const Value<1> strokeWidth = evaluate(group.stroke.width, frame);
                const EvaluatedTrimPaths trim =
                    evaluate_trim_paths(group.trim, frame);

                for (const Path& path : group.paths)
                {
                    const bool trimHidesStroke =
                        trim.present && trim.extent <= 0.000001;
                    svg << "<path d=\"";
                    append_trimmed_path_data(svg, path, trim);
                    svg << "\" fill=\"" << (group.fill.present ? svg_color(fillColor) : "none")
                        << "\" fill-opacity=\""
                        << (group.fill.present
                            ? std::clamp(
                                fillOpacity[0] * 0.01 * fillColor[3] * inheritedOpacity,
                                0.0,
                                1.0)
                            : 0.0)
                        << "\" fill-rule=\"" << (group.evenOddFill ? "evenodd" : "nonzero")
                        << "\" stroke=\"" << (group.stroke.present ? svg_color(strokeColor) : "none")
                        << "\" stroke-opacity=\""
                        << (group.stroke.present
                            ? std::clamp(
                                strokeOpacity[0] * 0.01 * strokeColor[3] *
                                    inheritedOpacity * (trimHidesStroke ? 0.0 : 1.0),
                                0.0,
                                1.0)
                            : 0.0)
                        << "\" stroke-width=\"" << std::max(strokeWidth[0], 0.0)
                        << "\" stroke-linecap=\"" << svg_line_cap(group.stroke.lineCap)
                        << "\" stroke-linejoin=\"" << svg_line_join(group.stroke.lineJoin)
                        << "\"/>";
                }
                svg << "</g>";
            }
            svg << "</g>";
        }
        svg << "</svg>";
        return svg.str();
    }
}

namespace vibrance::animation
{
    LottieSvgAnimation make_lottie_svg_animation(
        std::string_view json,
        uint32_t requestedFrameCount,
        uint32_t maximumFrameCount)
    {
        LottieSvgAnimation animation;
        if (json.empty())
        {
            animation.error = "Lottie file is empty.";
            return animation;
        }

        simdjson::dom::parser parser;
        Json root;
        // simdjson performs wide SIMD reads and therefore requires its
        // documented padding even when the source began as std::string.
        const simdjson::padded_string jsonText(json);
        const simdjson::error_code parseError = parser.parse(jsonText).get(root);
        if (parseError != simdjson::SUCCESS)
        {
            animation.error = std::string("Invalid Lottie JSON: ") +
                simdjson::error_message(parseError);
            return animation;
        }

        Document document;
        if (!parse_document(root, document, animation.error))
        {
            return animation;
        }

        const double sourceFrameSpan = document.outFrame - document.inFrame;
        const double durationSeconds = sourceFrameSpan / document.frameRate;
        uint32_t frameCount = requestedFrameCount > 0u
            ? requestedFrameCount
            : static_cast<uint32_t>(std::max(std::ceil(sourceFrameSpan), 1.0));
        if (maximumFrameCount > 0u)
        {
            frameCount = std::min(frameCount, maximumFrameCount);
        }
        frameCount = std::max(frameCount, 1u);

        animation.width = document.width;
        animation.height = document.height;
        animation.durationSeconds = durationSeconds;
        animation.frameRate = document.frameRate;
        animation.ignoredExpressionCount = document.ignoredExpressionCount;
        animation.unsupportedFeatureCount = document.unsupportedFeatureCount;
        animation.frames.reserve(frameCount);

        const double frameDuration = durationSeconds / static_cast<double>(frameCount);
        for (uint32_t index = 0u; index < frameCount; ++index)
        {
            const double progress = static_cast<double>(index) /
                static_cast<double>(frameCount);
            const double frame = document.inFrame + sourceFrameSpan * progress;
            animation.frames.push_back({
                render_svg_frame(document, frame),
                frameDuration
            });
        }
        return animation;
    }
}
