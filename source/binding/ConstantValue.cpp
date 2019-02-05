//------------------------------------------------------------------------------
// ConstantValue.cpp
// Compile-time constant representation.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "slang/binding/ConstantValue.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "slang/text/FormatBuffer.h"

namespace slang {

const ConstantValue ConstantValue::Invalid;

std::string ConstantValue::toString() const {
    return std::visit(
        [](auto&& arg) noexcept {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return "<unset>"s;
            else if constexpr (std::is_same_v<T, SVInt>)
                return arg.toString();
            else if constexpr (std::is_same_v<T, double>)
                return std::to_string(arg);
            else if constexpr (std::is_same_v<T, ConstantValue::NullPlaceholder>)
                return "null"s;
            else if constexpr (std::is_same_v<T, Array>) {
                FormatBuffer buffer;
                buffer.append("[");
                for (auto& element : arg) {
                    buffer.append(element.toString());
                    buffer.append(",");
                }

                if (!arg.empty())
                    buffer.pop_back();
                buffer.append("]");
                return buffer.str();
            }
            else
                static_assert(always_false<T>::value, "Missing case");
        },
        value);
}

ConstantValue ConstantValue::getSlice(int32_t upper, int32_t lower) const {
    if (isInteger())
        return integer().slice(upper, lower);

    if (isArray()) {
        auto elements = array().subspan(lower, upper - lower + 1);
        return std::vector<ConstantValue>(elements.begin(), elements.end());
    }

    return nullptr;
}

void to_json(json& j, const ConstantValue& cv) {
    j = cv.toString();
}

std::ostream& operator<<(std::ostream& os, const ConstantValue& cv) {
    return os << cv.toString();
}

ConstantRange ConstantRange::subrange(ConstantRange select) const {
    int32_t l = lower();
    ConstantRange result;
    result.left = select.lower() + l;
    result.right = select.upper() + l;

    ASSERT(result.right <= upper());
    if (isLittleEndian())
        return result;
    else
        return result.reverse();
}

int32_t ConstantRange::translateIndex(int32_t index) const {
    if (!isLittleEndian())
        return upper() - index;
    else
        return index - lower();
}

bool ConstantRange::containsPoint(int32_t index) const {
    return index >= lower() && index <= upper();
}

std::string ConstantRange::toString() const {
    return fmt::format("[{}:{}]", left, right);
}

std::ostream& operator<<(std::ostream& os, const ConstantRange& cr) {
    return os << cr.toString();
}

ConstantValue LValue::load() const {
    return std::visit(
        [](auto&& arg)
    // This ifdef is here until MS fixes a compiler regression
#ifndef _MSVC_LANG
            noexcept(!std::is_same_v<std::decay_t<decltype(arg)>, Concat>)
#endif
                ->ConstantValue {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>)
                        return ConstantValue();
                    else if constexpr (std::is_same_v<T, ConstantValue*>)
                        return *arg;
                    else if constexpr (std::is_same_v<T, CVRange>)
                        return arg.cv->getSlice(arg.range.upper(), arg.range.lower());
                    else if constexpr (std::is_same_v<T, Concat>)
                        THROW_UNREACHABLE; // TODO: handle this case
                    else
                        static_assert(always_false<T>::value, "Missing case");
                },
        value);
}

void LValue::store(const ConstantValue& newValue) {
    std::visit(
        [&newValue](auto&& arg) noexcept(!std::is_same_v<std::decay_t<decltype(arg)>, Concat>) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return;
            else if constexpr (std::is_same_v<T, ConstantValue*>)
                *arg = newValue;
            else if constexpr (std::is_same_v<T, CVRange>) {
                ConstantValue& cv = *arg.cv;
                ASSERT(cv);

                int32_t l = arg.range.lower();
                int32_t u = arg.range.upper();

                if (cv.isArray()) {
                    auto src = newValue.array();
                    auto dest = cv.array();

                    for (int32_t i = l; i <= u; i++)
                        dest[i] = src[i - l];
                }
                else {
                    cv.integer().set(u, l, newValue.integer());
                }
            }
            else if constexpr (std::is_same_v<T, Concat>)
                THROW_UNREACHABLE; // TODO: handle this case
            else
                static_assert(always_false<T>::value, "Missing case");
        },
        value);
}

LValue LValue::selectRange(ConstantRange range) const {
    return std::visit(
        [&range](auto&& arg) noexcept(!std::is_same_v<std::decay_t<decltype(arg)>, Concat>)
            ->LValue {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return nullptr;
                else if constexpr (std::is_same_v<T, ConstantValue*>)
                    return LValue(*arg, range);
                else if constexpr (std::is_same_v<T, CVRange>)
                    return LValue(*arg.cv, arg.range.subrange(range));
                else if constexpr (std::is_same_v<T, Concat>)
                    THROW_UNREACHABLE;
                else
                    static_assert(always_false<T>::value, "Missing case");
            },
        value);
}

LValue LValue::selectIndex(int32_t index) const {
    return std::visit(
        [index](auto&& arg) noexcept(!std::is_same_v<std::decay_t<decltype(arg)>, Concat>)->LValue {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return nullptr;
            else if constexpr (std::is_same_v<T, ConstantValue*>)
                return LValue(arg->array()[index]);
            else if constexpr (std::is_same_v<T, CVRange>)
                return LValue(arg.cv->array()[arg.range.lower() + index]);
            else if constexpr (std::is_same_v<T, Concat>)
                THROW_UNREACHABLE;
            else
                static_assert(always_false<T>::value, "Missing case");
        },
        value);
}

} // namespace slang
