#ifndef NONLOCAL_GENERAL_CONFIG_DATA_HPP
#define NONLOCAL_GENERAL_CONFIG_DATA_HPP

#include "config_utils.hpp"

#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <optional>

namespace nonlocal::config {

class save_data final {
    std::filesystem::path _folder;
    std::unordered_map<std::string, std::string> _names;

public:
    explicit save_data(const Json::Value& save);

    bool contains(const std::string& key) const;
    const std::filesystem::path& folder() const noexcept;
    std::filesystem::path path(const std::string& key, const std::string& extension,
                               const std::optional<std::string>& default_name = std::nullopt) const;
};

template<std::floating_point T>
struct nonstationary_data final {
    T time_step = T{0.01};      // required
    T initial_time = T{0};
    uint64_t steps_cont = 100u; // required
    uint64_t save_frequency = 1u;

    explicit constexpr nonstationary_data() noexcept = default;
    explicit nonstationary_data(const Json::Value& nonstationary) {
        check_required_fields(nonstationary, { "time_step", "steps_count"});
        time_step = nonstationary["time_step"].template as<T>();
        initial_time = nonstationary.get("initial_time", T{0}).template as<T>();
        steps_cont = nonstationary["steps_count"].asUInt64();
        save_frequency = nonstationary.get("save_frequency", 1u).asUInt64();
    }
};

template<std::floating_point T, size_t Dimension>
class model_data final {
    using radius_t = std::conditional_t<Dimension == 1, T, std::array<T, Dimension>>;

    static radius_t read_radius(const Json::Value& arr, const std::string& field) {
        std::array<T, Dimension> result;
        
        if (arr.template is<T>())
            result.fill(arr.template as<T>());
        else if (arr.isArray() && arr.size() == Dimension)
            for(const Json::ArrayIndex i : std::ranges::iota_view{0u, Dimension})
                result[i] = arr[i].template as<T>();
        else
            throw std::domain_error{"Field \"" + field + "\" must be an array with length " + std::to_string(Dimension)};

        if constexpr (std::is_same_v<radius_t, T>)
            return result.front();
        else
            return result;
    }

public:
    T local_weight = T{1};           // required
    radius_t nonlocal_radius = T{0}; // required
    radius_t search_radius = T{0};   // if skipped sets equal nonlocal_radius

    explicit constexpr model_data() noexcept = default;
    explicit model_data(const Json::Value& model) {
        check_required_fields(model, { "local_weight", "nonlocal_radius" });
        local_weight = model["local_weight"].template as<T>();
        nonlocal_radius = read_radius(model["nonlocal_radius"], "nonlocal_radius");
        search_radius = !model.isMember("search_radius") ? nonlocal_radius :
                        read_radius(model["search_radius"], "search_radius");
    }
};

template<std::floating_point T, template<std::floating_point, size_t> class Physics>
struct segment_data final {
    size_t elements_count = 100u; // required
    T length = T{1};              // required
    Physics<T, 1> physical;       // required
    model_data<T, 1> model;

    explicit constexpr segment_data() noexcept = default;
    explicit segment_data(const Json::Value& segment) {
        check_required_fields(segment, { "elements_count", "length", "physical" });
        elements_count = segment["elements_count"].asUInt64();
        length = segment["length"].template as<T>();
        physical = Physics<T, 1>{segment["physical"]};
        if (segment.isMember("model"))
            model = model_data<T, 1>{segment["model"]};
    }
};

}

#endif