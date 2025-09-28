#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <type_traits>
#include <functional>
#include <stdexcept>

namespace presence_for_plex {
namespace core {

// Simple dependency injection container
class DependencyContainer {
public:
    // Register a singleton service
    template<typename Interface, typename Implementation>
    void register_singleton(std::shared_ptr<Implementation> instance) {
        static_assert(std::is_base_of_v<Interface, Implementation>,
                      "Implementation must derive from Interface");

        auto type_id = std::type_index(typeid(Interface));
        m_services[type_id] = std::static_pointer_cast<void>(instance);
    }


    // Resolve a service
    template<typename Interface>
    std::shared_ptr<Interface> resolve() {
        auto type_id = std::type_index(typeid(Interface));

        // Check for singleton
        auto service_it = m_services.find(type_id);
        if (service_it != m_services.end()) {
            return std::static_pointer_cast<Interface>(service_it->second);
        }

        throw std::runtime_error("Service not registered: " + std::string(typeid(Interface).name()));
    }


private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> m_services;
};


} // namespace core
} // namespace presence_for_plex