#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
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
        m_factories[type_id] = nullptr; // Clear any factory
    }

    // Register a factory for creating services
    template<typename Interface>
    void register_factory(std::function<std::shared_ptr<Interface>()> factory) {
        auto type_id = std::type_index(typeid(Interface));
        m_factories[type_id] = [factory]() -> std::shared_ptr<void> {
            return std::static_pointer_cast<void>(factory());
        };
        m_services.erase(type_id); // Clear any singleton
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

        // Check for factory
        auto factory_it = m_factories.find(type_id);
        if (factory_it != m_factories.end() && factory_it->second) {
            auto instance = factory_it->second();
            return std::static_pointer_cast<Interface>(instance);
        }

        throw std::runtime_error("Service not registered: " + std::string(typeid(Interface).name()));
    }

    // Check if a service is registered
    template<typename Interface>
    bool is_registered() const {
        auto type_id = std::type_index(typeid(Interface));
        return m_services.contains(type_id) || m_factories.contains(type_id);
    }

    // Clear all registrations
    void clear() {
        m_services.clear();
        m_factories.clear();
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> m_services;
    std::unordered_map<std::type_index, std::function<std::shared_ptr<void>()>> m_factories;
};

// Service locator pattern for global access (use sparingly)
class ServiceLocator {
public:
    static DependencyContainer& get_container() {
        static DependencyContainer container;
        return container;
    }

    template<typename Interface>
    static std::shared_ptr<Interface> resolve() {
        return get_container().resolve<Interface>();
    }

private:
    ServiceLocator() = default;
};

// Builder pattern for dependency injection
template<typename T>
class ServiceBuilder {
public:
    ServiceBuilder() = default;

    template<typename Dependency>
    ServiceBuilder& with_dependency(std::shared_ptr<Dependency> dep) {
        m_dependencies[std::type_index(typeid(Dependency))] = dep;
        return *this;
    }

    std::shared_ptr<T> build() {
        return build_impl();
    }

protected:
    template<typename Dependency>
    std::shared_ptr<Dependency> get_dependency() const {
        auto type_id = std::type_index(typeid(Dependency));
        auto it = m_dependencies.find(type_id);
        if (it != m_dependencies.end()) {
            return std::static_pointer_cast<Dependency>(it->second);
        }
        throw std::runtime_error("Missing dependency: " + std::string(typeid(Dependency).name()));
    }

private:
    virtual std::shared_ptr<T> build_impl() = 0;

    std::unordered_map<std::type_index, std::shared_ptr<void>> m_dependencies;
};

} // namespace core
} // namespace presence_for_plex