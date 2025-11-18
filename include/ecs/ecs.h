/*

MIT License

Copyright (c) 2025 | Martin Starkov | https://github.com/martinstarkov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef NDEBUG
#define ECS_ENABLE_ASSERTS
#endif

#ifdef ECS_ENABLE_ASSERTS

#include <cstdlib>
#include <filesystem>
#include <iostream>

/**
 * @brief Macro to trigger a debug break for debugging purposes.
 */
#define ECS_DEBUGBREAK() ((void)0)

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() __debugbreak()
#elif defined(__linux__)
#include <signal.h>
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() raise(SIGTRAP)
#endif

/**
 * @brief Macro for assertions with condition checking.
 *
 * If the condition fails, prints an error message along with the file and line number
 * where the assertion occurred and triggers a debug break, then aborts the program.
 *
 * @param condition The condition to test for.
 * @param message The error message to display on failure.
 */
#define ECS_ASSERT(condition, message)                                                          \
	{                                                                                           \
		if (!(condition)) {                                                                     \
			std::cout << "ECS ASSERTION FAILED: "                                               \
					  << std::filesystem::path(__FILE__).filename().string() << ":" << __LINE__ \
					  << ": " << message << "\n";                                               \
			ECS_DEBUGBREAK();                                                                   \
			std::abort();                                                                       \
		}                                                                                       \
	}

#else

#define ECS_ASSERT(...) ((void)0)

#endif

namespace ecs {

namespace impl {

template <typename TArchiver>
class EntityHandle;

template <typename TArchiver>
class Manager;

using Id	  = std::uint32_t;
using Version = std::uint32_t;

/**
 * @brief Enum for defining loop criteria for iterating through entities.
 */
enum class LoopCriterion {
	None,
	WithComponents,
	WithoutComponents
};

template <
	typename TEntityHandle, typename TArchiver, bool is_const, LoopCriterion Criterion,
	typename... TComponents>
class View;

template <LoopCriterion Criterion, typename TView, typename... TComponents>
class ViewIterator;

} // namespace impl

/**
 * @brief Alias for an entity view with no components for const and non-const entities.
 *
 * @tparam is_const A boolean indicating whether the view is for const entities.
 */
template <typename TArchiver, bool is_const>
using View =
	impl::View<impl::EntityHandle<TArchiver>, TArchiver, is_const, impl::LoopCriterion::None>;

/**
 * @brief Alias for an entity view with specific components for const and non-const entities.
 *
 * @tparam is_const A boolean indicating whether the view is for const entities.
 * @tparam TComponents The component types that entities must have.
 */
template <typename TArchiver, bool is_const, typename... TComponents>
using ViewWith = impl::View<
	impl::EntityHandle<TArchiver>, TArchiver, is_const, impl::LoopCriterion::WithComponents,
	TComponents...>;

/**
 * @brief Alias for an entity view lacking specific components for const and non-const
 * entities.
 *
 * @tparam is_const A boolean indicating whether the view is for const entities.
 * @tparam TComponents The component types that entities must lack.
 */
template <typename TArchiver, bool is_const, typename... TComponents>
using ViewWithout = impl::View<
	impl::EntityHandle<TArchiver>, TArchiver, is_const, impl::LoopCriterion::WithoutComponents,
	TComponents...>;

/**
 * @brief A class that wraps a callable hook (either free function or member function).
 *
 * @tparam Ret Return type of the hook.
 * @tparam Args Argument types for the hook.
 */
template <typename Ret, typename... Args>
class Hook {
public:
	/// Function type for the hook implementation.
	using FunctionType = Ret (*)(void*, Args...);
	/// Return type of the hook.
	using ReturnType = Ret;

	Hook() = default;

	/**
	 * @brief Connects a free (non-member) function to this hook.
	 *
	 * @tparam Function A free function pointer of type Ret(Args...).
	 * @return *this.
	 */
	template <auto Function>
	Hook& Connect() noexcept {
		fn_ = [](void*, Args... args) -> Ret {
			return std::invoke(Function, std::forward<Args>(args)...);
		};
		instance_ = nullptr;
		return *this;
	}

	/**
	 * @brief Connects a non-capturing lambda to this hook.
	 *
	 * @tparam Lambda Type of the lambda or function object.
	 * @param lambda A non-capturing lambda to connect.
	 * @return Reference to this HookImpl instance.
	 *
	 * @note Capturing lambdas are not supported in this overload, as they cannot
	 *       be converted to function pointers in C++17.
	 */
	template <typename Lambda>
	Hook& Connect(Lambda lambda) noexcept {
		fn_		  = +lambda;
		instance_ = nullptr;
		return *this;
	}

	/**
	 * @brief Connects a member function to this hook.
	 *
	 * @tparam Type The class type that contains the member function.
	 * @tparam Member A member function pointer of type Ret(Type::*)(Args...).
	 * @param obj Pointer to the instance of the object.
	 * @return *this.
	 */
	template <typename Type, auto Member>
	Hook& Connect(Type* obj) noexcept {
		fn_ = [](void* instance, Args... args) -> Ret {
			return (static_cast<Type*>(instance)->*Member)(std::forward<Args>(args)...);
		};
		instance_ = obj;
		return *this;
	}

	/**
	 * @brief Invokes the connected function with the given arguments.
	 *
	 * @param args Arguments to pass to the function.
	 * @return Result of the function call.
	 */
	template <typename... TArgs>
	Ret operator()(TArgs... args) const {
		return std::invoke(fn_, instance_, std::forward<TArgs>(args)...);
	}

	friend bool operator==(const Hook& a, const Hook& b) {
		return a.fn_ == b.fn_ && a.instance_ == b.instance_;
	}

	friend bool operator!=(const Hook& a, const Hook& b) {
		return !operator==(a, b);
	}

private:
	// @brief Function pointer with void* for object binding.
	FunctionType fn_{ nullptr };

	// @brief Pointer to the object instance for member functions.
	void* instance_{ nullptr };
};

namespace impl {

namespace tt {

template <typename Struct, typename = void, typename... Ts>
struct is_direct_list_initializable_impl : std::false_type {};

template <typename Struct, typename... Ts>
struct is_direct_list_initializable_impl<
	Struct, std::void_t<decltype(Struct{ std::declval<Ts>()... })>, Ts...> : std::true_type {};

template <typename Struct, typename... Ts>
using is_direct_list_initializable = is_direct_list_initializable_impl<Struct, void, Ts...>;

template <typename Struct, typename... Ts>
constexpr bool is_direct_list_initializable_v = is_direct_list_initializable<Struct, Ts...>::value;

template <typename Struct, typename... Ts>
struct aggregate_initializable {
	constexpr static const bool value{ std::conjunction_v<
		std::is_aggregate<Struct>, is_direct_list_initializable<Struct, Ts...>,
		std::negation<std::conjunction<
			std::bool_constant<sizeof...(Ts) == 1>,
			std::is_same<std::decay_t<std::tuple_element_t<0, std::tuple<Ts...>>>, Struct>>>> };
};

template <typename S>
struct aggregate_initializable<S> : std::false_type {};

template <typename Struct, typename... Ts>
constexpr bool is_aggregate_initializable_v = aggregate_initializable<Struct, Ts...>::value;

} // namespace tt

/**
 * @class VoidArchiver
 * @brief A no-op archiver that performs no serialization or deserialization.
 *
 * This class acts as a placeholder archiver when serialization is not required.
 * It provides interface-compatible methods that do nothing, allowing components
 * and pools to be instantiated without needing real serialization behavior.
 */
class VoidArchiver {
public:
	template <typename TComponent>
	void WriteComponent(const TComponent& component);

	template <typename TComponent>
	[[nodiscard]] bool HasComponent() const;

	template <typename TComponent>
	[[nodiscard]] TComponent ReadComponent() const;

	template <typename TComponent>
	void WriteComponents(const std::vector<TComponent>& value);

	void SetDenseSet(const std::vector<Id>& dense_set);
	void SetSparseSet(const std::vector<Id>& sparse_set);

	template <typename TComponent>
	[[nodiscard]] std::vector<TComponent> ReadComponents() const;

	[[nodiscard]] std::vector<Id> GetDenseSet() const;
	[[nodiscard]] std::vector<Id> GetSparseSet() const;
};

/**
 * @class AbstractPool
 * @brief Abstract base class for managing pools of components.
 *
 * This class defines the interface for managing a pool of components associated with entities.
 * It is intended to be subclassed for concrete component types.
 *
 * @tparam TArchiver A class responsible for serializing component data. Defaults to VoidArchiver,
 *                  which performs no serialization and can be used as a placeholder when
 * serialization is not needed.
 */
template <typename TArchiver = VoidArchiver>
class AbstractPool {
public:
	virtual ~AbstractPool() = default;

	// @return True if the components of the pool can be cloned, otherwise false.
	[[nodiscard]] virtual bool IsCloneable() const = 0;

	// @return A unique pointer to a new instance of the pool.
	[[nodiscard]] virtual std::unique_ptr<AbstractPool> Clone() const = 0;

	// @param manager The manager which owns the pool.
	virtual void InvokeDestructHooks(const Manager<TArchiver>& manager) = 0;

	/**
	 * @brief Copies a component from one entity to another within the pool.
	 *
	 * @param manager The manager within which the copy is occurring.
	 */
	virtual void Copy(const Manager<TArchiver>& manager, Id from_entity, Id to_entity) = 0;

	/**
	 * @brief Removes all components from the pool but does not reset the size of the pool.
	 *
	 * @param manager The manager to which the component pool belongs.
	 */
	virtual void Clear(const Manager<TArchiver>& manager) = 0;

	/**
	 * @brief Clears the pool and shrinks its memory usage to fit the current size.
	 *
	 * @param manager The manager to which the component pool belongs.
	 */
	virtual void Reset(const Manager<TArchiver>& manager) = 0;

	/**
	 * @brief Removes a component from the pool for a given entity.
	 *
	 * @param manager The manager to which the entity belongs.
	 * @return True if the component was successfully removed, otherwise false.
	 */
	virtual bool Remove(const Manager<TArchiver>& manager, Id entity) = 0;

	/**
	 * @brief Checks if the pool contains a component for the specified entity.
	 *
	 * @return True if the entity has a component in the pool, otherwise false.
	 */
	[[nodiscard]] virtual bool Has(Id entity) const = 0;

	/**
	 * @brief Calls the update hooks of the component for the specified entity.
	 *
	 * @param manager The manager to which the entity belongs.
	 * @param entity The entity to update.
	 */
	virtual void Update(const Manager<TArchiver>& manager, Id entity) const = 0;

	/**
	 * @brief Serializes all components in the pool using the provided archiver.
	 *
	 * This function should be implemented by derived classes to serialize their internal component
	 * data into the given archiver. The format and behavior of serialization depends on the
	 * specific TArchiver type.
	 *
	 * @param archiver The archiver instance used to write the serialized component data.
	 */
	virtual void Serialize(TArchiver& archiver) const = 0;

	/**
	 * @brief Serializes the component associated with a specific entity using the provided
	 * archiver.
	 *
	 * This function should be implemented by derived classes to serialize the internal data
	 * of a single component identified by the given entity index. The serialized output
	 * must be consistent with the format expected by the corresponding deserialization function.
	 *
	 * @param archiver The archiver instance used to write the serialized component data.
	 * @param entity The index of the entity whose component should be serialized.
	 */
	virtual void Serialize(TArchiver& archiver, Id entity) const = 0;

	/**
	 * @brief Deserializes components into the pool using the provided archiver.
	 *
	 * This function should be implemented by derived classes to reconstruct their internal
	 * component data from the given archiver. The format must match that used during serialization.
	 *
	 * @param archiver The archiver instance used to read and load the component data.
	 */
	virtual void Deserialize(const TArchiver& archiver) = 0;

	/**
	 * @brief Deserializes component data for a specific entity from the provided archiver.
	 *
	 * This function should be implemented by derived classes to reconstruct the internal
	 * component data for a single entity from the archiver. The input format must match
	 * the one used during serialization of that entity's component.
	 *
	 * @param archiver The archiver instance used to read and load the component data.
	 * @param manager The manager that the entity belongs to.
	 * @param entity The index of the entity for which the component data should be deserialized.
	 */
	virtual void Deserialize(
		const TArchiver& archiver, const Manager<TArchiver>& manager, Id entity
	) = 0;
};

/**
 * @brief A view for storing and managing multiple hooks.
 *
 * @tparam Ret Return type for all hooks in the pool.
 * @tparam Args Argument types for all hooks in the pool.
 */
template <typename Ret, typename... Args>
class HookPool {
public:
	/// Type alias for the hook implementation.
	using HookType = ecs::Hook<Ret, Args...>;

	/**
	 * @brief Adds a new default-initialized hook to the pool.
	 *
	 * @return Reference to the newly added hook.
	 */
	HookType& AddHook() {
		return hooks_.emplace_back();
	}

	// @return True if the hook pool has the specified hook, false otherwise.
	[[nodiscard]] bool HasHook(const HookType& hook) const {
		auto it{ std::find(hooks_.begin(), hooks_.end(), hook) };
		return it != hooks_.end();
	}

	void RemoveHook(const HookType& hook) {
		auto it{ std::find(hooks_.begin(), hooks_.end(), hook) };
		ECS_ASSERT(it != hooks_.end(), "Cannot remove hook which has not been added");
		hooks_.erase(it);
	}

	/**
	 * @brief Invokes all hooks in the pool with the given arguments.
	 *
	 * @param args Arguments to pass to each hook.
	 */
	void Invoke(Args... args) const {
		for (const auto& hook : hooks_) {
			std::invoke(hook, args...);
		}
	}

private:
	// @brief Adds an existing hook to the pool.
	void AddHook(const HookType& hook) {
		hooks_.emplace_back(hook);
	}

	std::vector<HookType> hooks_;
};

template <typename TArchiver>
using ComponentHooks = HookPool<void, EntityHandle<TArchiver>>;

/**
 * @class Pool
 * @brief A template class representing a pool of components of type TComponent with optional
 * serialization support.
 *
 * This class manages a collection of components of type TComponent, providing efficient storage,
 * access, and manipulation of components associated with entities. It inherits from
 * AbstractPool and supports optional serialization through a customizable TArchiver.
 *
 * @tparam TComponent         The type of component stored in the pool.
 * @tparam TArchiver  The archiver used for serialization and deserialization of components.
 *                   If set to VoidArchiver, serialization is effectively disabled.
 */
template <typename TComponent, typename TArchiver>
class Pool : public AbstractPool<TArchiver> {
	static_assert(
		std::is_move_constructible_v<TComponent>,
		"Cannot create pool for component which is not move constructible"
	);
	static_assert(
		std::is_destructible_v<TComponent>,
		"Cannot create pool for component which is not destructible"
	);

public:
	Pool()							 = default;
	Pool(Pool&&) noexcept			 = default;
	Pool& operator=(Pool&&) noexcept = default;
	Pool(const Pool&)				 = default;
	Pool& operator=(const Pool&)	 = default;
	~Pool() noexcept override		 = default;

	/**
	 * @brief Serializes the component data in the pool using the provided archiver.
	 *
	 * @param archiver The archiver instance used to write component data.
	 */
	void Serialize(TArchiver& archiver) const override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver>) {
			archiver.template WriteComponents<TComponent>(components);
			archiver.SetDenseSet(dense);
			archiver.SetSparseSet(sparse);
		}
	}

	/**
	 * @brief Serializes the component data of a single entity using the provided archiver.
	 *
	 * @param archiver The archiver instance used to write component data.
	 * @param entity The index of the entity whose component should be serialized.
	 */
	void Serialize(TArchiver& archiver, Id entity) const override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver>) {
			if (Has(entity)) {
				archiver.template WriteComponent<TComponent>(Get(entity));
			}
		}
	}

	/**
	 * @brief Deserializes component data into the pool using the provided archiver.
	 *
	 * @param archiver The archiver instance used to read component data.
	 */
	void Deserialize(const TArchiver& archiver) override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver> &&
					  std::is_default_constructible_v<TComponent>) {
			components = archiver.template ReadComponents<TComponent>();
			dense	   = archiver.GetDenseSet();
			sparse	   = archiver.GetSparseSet();
		}
	}

	/**
	 * @brief Deserializes component data for a single entity using the provided archiver.
	 *
	 * @param archiver The archiver instance used to read component data.
	 * @param manager The manager that the entity belongs to.
	 * @param entity The index of the entity for which the component data should be deserialized.
	 */
	void Deserialize(const TArchiver& archiver, const Manager<TArchiver>& manager, Id entity)
		override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver> &&
					  std::is_default_constructible_v<TComponent>) {
			if (!archiver.template HasComponent<TComponent>()) {
				return;
			}
			if (Has(entity)) {
				Get(entity) = archiver.template ReadComponent<TComponent>();
			} else {
				Add(manager, entity, archiver.template ReadComponent<TComponent>());
			}
		}
	}

	/**
	 * @brief Checks if the pool's components are cloneable.
	 *
	 * @return True if the components are copy constructible, otherwise false.
	 */
	[[nodiscard]] bool IsCloneable() const override {
		return std::is_copy_constructible_v<TComponent>;
	}

	/**
	 * @brief Clones the pool and returns a new instance with the same data.
	 *
	 * @return A unique pointer to a new pool instance with the same components.
	 */
	[[nodiscard]] std::unique_ptr<AbstractPool<TArchiver>> Clone() const override {
		// The reason this is not statically asserted is because it would disallow move-only
		// component pools.
		if constexpr (std::is_copy_constructible_v<TComponent>) {
			auto pool{ std::make_unique<Pool<TComponent, TArchiver>>() };
			pool->components	  = components;
			pool->dense			  = dense;
			pool->sparse		  = sparse;
			pool->construct_hooks = construct_hooks;
			pool->destruct_hooks  = destruct_hooks;
			pool->update_hooks	  = update_hooks;
			return pool;
		} else {
			ECS_ASSERT(
				false, "Cannot clone component pool with a non copy constructible component"
			);
			return nullptr;
		}
	}

	/**
	 * @brief Invokes destruct hooks on all the components in the pool.
	 *
	 * @param manager The manager which owns the pool.
	 */
	void InvokeDestructHooks(const Manager<TArchiver>& manager) override;

	/**
	 * @brief Copies a component from one entity to another.
	 *
	 * @param manager The manager within which the copy is occurring.
	 * @param from_entity The source entity from which to copy the component.
	 * @param to_entity The target entity to which the component will be copied.
	 */
	void Copy(const Manager<TArchiver>& manager, Id from_entity, Id to_entity) override;

	/**
	 * @brief Clears the pool, removing all components.
	 *
	 * This method removes all components from the pool.
	 *
	 * @param manager The manager to which the component pool belongs.
	 */
	void Clear(const Manager<TArchiver>& manager) override;

	/**
	 * @brief Resets the pool by clearing all components and shrinking memory usage.
	 *
	 * @param manager The manager to which the component pool belongs.
	 */
	void Reset(const Manager<TArchiver>& manager) override {
		Clear(manager);

		components.shrink_to_fit();
		dense.shrink_to_fit();
		sparse.shrink_to_fit();
	}

	/**
	 * @brief Removes a component from the pool for a given entity.
	 *
	 * @param manager The manager to which the entity belongs.
	 * @param entity The entity from which to remove the component.
	 * @return True if the component was successfully removed, otherwise false.
	 */
	bool Remove(const Manager<TArchiver>& manager, Id entity) override;

	/**
	 * @brief Checks if the pool contains a component for the specified entity.
	 *
	 * @param entity The entity to check.
	 * @return True if the entity has a component in the pool, otherwise false.
	 */
	[[nodiscard]] bool Has(Id entity) const override {
		if (entity >= sparse.size()) {
			return false;
		}
		auto s{ sparse[entity] };
		if (s >= dense.size()) {
			return false;
		}
		return entity == dense[s];
	}

	/**
	 * @brief Calls the update hooks of the component for the specified entity.
	 *
	 * @param manager The manager to which the entity belongs.
	 * @param entity The entity to update.
	 */
	virtual void Update(const Manager<TArchiver>& manager, Id entity) const override;

	/**
	 * @brief Retrieves a constant reference to the component associated with the specified entity.
	 *
	 * @param entity The entity whose component is to be retrieved.
	 * @return A constant reference to the component associated with the entity.
	 */
	[[nodiscard]] const TComponent& Get(Id entity) const {
		ECS_ASSERT(Has(entity), "EntityHandle does not have the requested component");
		ECS_ASSERT(
			sparse[entity] < components.size(),
			"Likely attempting to retrieve a component before it has been fully added"
		);
		return components[sparse[entity]];
	}

	/**
	 * @brief Retrieves a reference to the component associated with the specified entity.
	 *
	 * @param entity The entity whose component is to be retrieved.
	 * @return A reference to the component associated with the entity.
	 */
	[[nodiscard]] TComponent& Get(Id entity) {
		return const_cast<TComponent&>(std::as_const(*this).Get(entity));
	}

	/**
	 * @return Number of components in the pool.
	 */
	[[nodiscard]] std::size_t Size() const noexcept {
		return components.size();
	}

	/**
	 * @brief Adds a component to the pool for the specified entity.
	 *
	 * @param manager The manager to which the entity belongs.
	 * @param entity The entity to which the component will be added.
	 * @param constructor_args Arguments to construct the component.
	 * @return A reference to the newly added component.
	 */
	template <typename... TArgs>
	TComponent& Add(const Manager<TArchiver>& manager, Id entity, TArgs&&... constructor_args);

	std::vector<TComponent> components;

	// Indices of the components.
	std::vector<Id> dense;
	// Indices of the entities in the dense set.
	std::vector<Id> sparse;

	ComponentHooks<TArchiver> construct_hooks;
	ComponentHooks<TArchiver> update_hooks;
	ComponentHooks<TArchiver> destruct_hooks;
};

/**
 * @class Pools
 * @brief A template class for managing multiple pools of components.
 *
 * This class allows for accessing multiple pools of components (of different types) in a type-safe
 * manner.
 */
template <typename TEntityHandle, typename TArchiver, bool is_const, typename... TComponents>
class Pools {
public:
	template <typename TPool>
	using PoolType =
		std::conditional_t<is_const, const Pool<TPool, TArchiver>*, Pool<TPool, TArchiver>*>;

	explicit constexpr Pools(PoolType<TComponents>... pools) :
		pools_{ std::tuple<PoolType<TComponents>...>(pools...) } {}

	template <typename TComponent>
	constexpr const auto* GetPool() const {
		return std::get<PoolType<TComponent>>(pools_);
	}

	template <typename TComponent>
	constexpr auto* GetPool() {
		return std::get<PoolType<TComponent>>(pools_);
	}

	// @brief Copies components from one entity to another across all pools.
	constexpr void Copy(const Manager<TArchiver>& manager, Id from_id, Id to_id) {
		(GetPool<TComponents>()->template Pool<TComponents, TArchiver>::Copy(
			 manager, from_id, to_id
		 ),
		 ...);
	}

	// @return True if the entity exists in all pools, otherwise false.
	[[nodiscard]] constexpr bool Has(Id entity) const {
		ECS_ASSERT(
			AllExist(), "Component pools cannot be destroyed while stored in a Pools object"
		);
		return (GetPool<TComponents>()->template Pool<TComponents, TArchiver>::Has(entity) && ...);
	}

	// @return True if the entity is missing at least one of the requested components, otherwise
	// false.
	[[nodiscard]] constexpr bool NotHas(Id entity) const {
		return ((GetPool<TComponents>() == nullptr) || ...) ||
			   (!GetPool<TComponents>()->template Pool<TComponents, TArchiver>::Has(entity) && ...);
	}

	// @return An entity handle followed by a tuple of the requested components.
	[[nodiscard]] constexpr decltype(auto) GetWithEntity(
		Id entity, const Manager<TArchiver>* manager
	) const;

	// @return An entity handle followed by a tuple of the requested components.
	[[nodiscard]] constexpr decltype(auto) GetWithEntity(Id entity, Manager<TArchiver>* manager);

	// @return A tuple of references to the requested components.
	[[nodiscard]] constexpr decltype(auto) Get(Id entity) const {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(TComponents) > 0);
		if constexpr (sizeof...(TComponents) == 1) {
			return (GetOne<TComponents>(entity), ...);
		} else {
			return std::forward_as_tuple(GetOne<TComponents>(entity)...);
		}
	}

	// @return A tuple of references to the requested components.
	[[nodiscard]] constexpr decltype(auto) Get(Id entity) {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(TComponents) > 0);
		if constexpr (sizeof...(TComponents) == 1) {
			return (GetOne<TComponents>(entity), ...);
		} else {
			return std::forward_as_tuple(GetOne<TComponents>(entity)...);
		}
	}

	template <typename TComponent>
	constexpr TComponent& GetOne(Id entity) {
		return GetPool<TComponent>()->template Pool<TComponent, TArchiver>::Get(entity);
	}

	template <typename TComponent>
	constexpr const TComponent& GetOne(Id entity) const {
		return GetPool<TComponent>()->template Pool<TComponent, TArchiver>::Get(entity);
	}

	// @return True if all pools exist, otherwise false.
	constexpr bool AllExist() const {
		return AllPools([](auto* pool) { return pool != nullptr; });
	}

	// @brief Helper to get the size of a specific component pool.
	template <typename TComponent>
	std::size_t Size() const noexcept {
		auto* pool = GetPool<TComponent>();
		return pool ? pool->Size() : 0;
	}

	template <typename F>
	constexpr void ForEachPool(F&& f) const {
		(f(GetPool<TComponents>()), ...);
	}

	template <typename F>
	constexpr bool AllPools(F&& f) const {
		return (f(GetPool<TComponents>()) && ...);
	}

private:
	// @brief A tuple containing all the pools managed by this class.
	std::tuple<PoolType<TComponents>...> pools_;
};

/**
 * @class DynamicBitset
 * @brief A dynamic bitset implementation that allows efficient bit-level operations.
 *
 * This class is a modified version of the dynamic_bitset and allows manipulation of individual
 * bits, as well as resizing, clearing, and reserving storage space for the bitset.
 */
class DynamicBitset {
	// Modified version of:
	// https://github.com/syoyo/dynamic_bitset/blob/master/dynamic_bitset.hh
public:
	DynamicBitset() = default;

	DynamicBitset(std::size_t bit_count, const std::vector<std::uint8_t>& data) :
		bit_count_{ bit_count }, data_{ data } {}

	/**
	 * @brief Sets the bit at the specified index to a given value.
	 *
	 * This method modifies the bit at the specified index to either true or false.
	 *
	 * @param index The index of the bit to be modified.
	 * @param value The value to set the bit to. Default is true.
	 */
	void Set(std::size_t index, bool value = true) {
		std::size_t byte_index{ index / 8 };
		std::uint8_t offset{ static_cast<std::uint8_t>(index % 8) };
		std::uint8_t bitfield{ static_cast<std::uint8_t>(1 << offset) };

		ECS_ASSERT(byte_index < data_.size(), "");

		if (value) {
			data_[byte_index] |= bitfield;
		} else {
			data_[byte_index] &= (~bitfield);
		}
	}

	/**
	 * @brief Gets the value of the bit at the specified index.
	 *
	 * This method returns whether the bit at the specified index is set to true (1) or false (0).
	 *
	 * @param index The index of the bit to check.
	 * @return True if the bit is set, otherwise false.
	 */
	[[nodiscard]] bool operator[](std::size_t index) const {
		std::size_t byte_index{ index / 8 };
		std::size_t offset{ index % 8 };

		ECS_ASSERT(byte_index < data_.size(), "");
		int set{ (data_[byte_index] >> offset) & 0x1 };
		return static_cast<bool>(set);
	}

	bool operator==(const DynamicBitset& other) const {
		return data_ == other.data_;
	}

	// @return The number of bits in the bitset.
	[[nodiscard]] std::size_t Size() const {
		return bit_count_;
	}

	// @return Returns the current capacity of the bitset. This is the number of bits that can be
	// stored before resizing the internal storage.
	[[nodiscard]] std::size_t Capacity() const {
		return data_.capacity();
	}

	/*
	 * @brief Reserves enough capacity to store a specific number of bits.
	 *
	 * This method ensures that the internal storage is large enough to hold the specified number of
	 * bits, without reallocation happening until the bitset grows beyond that size.
	 *
	 * @param new_capacity The number of bits for which to reserve capacity.
	 */
	void Reserve(std::size_t new_capacity) {
		auto byte_count{ GetByteCount(new_capacity) };
		data_.reserve(byte_count);
	}

	/**
	 * @brief Resizes the bitset to a new size, optionally initializing all bits to a value.
	 *
	 * This method changes the size of the bitset, either truncating or expanding it. Optionally,
	 * the new bits can be initialized to a specific value.
	 *
	 * @param new_size The new size of the bitset.
	 * @param value The value to initialize new bits to. Default is true.
	 */
	void Resize(std::size_t new_size, bool value) {
		auto byte_count{ GetByteCount(new_size) };
		bit_count_ = new_size;
		data_.resize(byte_count, value);
	}

	/**
	 * @brief Clears the bitset, resetting its size and data.
	 *
	 * This method resets the size of the bitset to zero and clears the underlying storage.
	 */
	void Clear() {
		bit_count_ = 0;
		data_.clear();
	}

	/**
	 * @brief Shrinks the capacity of the bitset to fit its current size.
	 *
	 * This method reduces the internal storage to the minimum required to store the current
	 * number of bits, helping to reduce memory usage.
	 */
	void ShrinkToFit() {
		data_.shrink_to_fit();
	}

	/**
	 * @brief Retrieves the bitset vector.
	 *
	 * @return The internal storage vector of bytes.
	 */
	[[nodiscard]] const std::vector<std::uint8_t>& GetData() const {
		return data_;
	}

private:
	/**
	 * @brief Calculates the number of bytes needed to store a given number of bits.
	 *
	 * @param bit_count The number of bits to store.
	 * @return The number of bytes required to store the specified number of bits.
	 */
	[[nodiscard]] static std::size_t GetByteCount(std::size_t bit_count) {
		std::size_t byte_count{ 1 };
		if (bit_count >= 8) {
			ECS_ASSERT(1 + (bit_count - 1) / 8 > 0, "");
			byte_count = 1 + (bit_count - 1) / 8;
		}
		return byte_count;
	}

	std::size_t bit_count_{ 0 };

	// TODO: Eventually move to using std::byte instead of std::uint8_t.
	std::vector<std::uint8_t> data_;
};

/**
 * @class Manager
 * @brief A class responsible for managing entities in the entity component system (ECS).
 *
 * The Manager class oversees the lifecycle of entities, including their creation, deletion,
 * refreshing, and component management within an entity-component system. It provides various
 * utility methods for manipulating entities, such as copying, querying, and clearing entities.
 * It also supports serialization and deserialization of entities and their components through
 * an optional TArchiver, allowing the state of the ECS to be saved or loaded.
 *
 * @tparam TArchiver The type of archiver used for serializing and deserializing entity and
 * component data. By default, the archiver is set to `VoidArchiver`, which disables serialization.
 */
template <typename TArchiver>
class Manager {
public:
	Manager() = default;

	Manager(const Manager& other) :
		next_entity_{ other.next_entity_ },
		count_{ other.count_ },
		refresh_required_{ other.refresh_required_ },
		entities_{ other.entities_ },
		refresh_{ other.refresh_ },
		versions_{ other.versions_ },
		free_entities_{ other.free_entities_ } {
		pools_.resize(other.pools_.size());
		for (std::size_t i{ 0 }; i < other.pools_.size(); ++i) {
			const auto& pool{ other.pools_[i] };
			if (pool == nullptr) {
				continue;
			}
			pools_[i] = pool->Clone();
			ECS_ASSERT(pools_[i] != nullptr, "Cloning manager failed");
		}
	}

	Manager& operator=(const Manager& other) {
		if (this != &other) {
			Reset();
			next_entity_	  = other.next_entity_;
			count_			  = other.count_;
			refresh_required_ = other.refresh_required_;
			entities_		  = other.entities_;
			refresh_		  = other.refresh_;
			versions_		  = other.versions_;
			free_entities_	  = other.free_entities_;
			for (std::size_t i{ 0 }; i < other.pools_.size(); ++i) {
				const auto& pool{ other.pools_[i] };
				if (pool == nullptr) {
					continue;
				}
				pools_[i] = pool->Clone();
				ECS_ASSERT(pools_[i] != nullptr, "Cloning manager failed");
			}
		}
		return *this;
	}

	Manager(Manager&& other) noexcept :
		next_entity_{ std::exchange(other.next_entity_, 0) },
		count_{ std::exchange(other.count_, 0) },
		refresh_required_{ std::exchange(other.refresh_required_, false) },
		entities_{ std::exchange(other.entities_, {}) },
		refresh_{ std::exchange(other.refresh_, {}) },
		versions_{ std::exchange(other.versions_, {}) },
		free_entities_{ std::exchange(other.free_entities_, {}) },
		pools_{ std::exchange(other.pools_, {}) } {}

	Manager& operator=(Manager&& other) noexcept {
		if (this != &other) {
			Reset();
			next_entity_	  = std::exchange(other.next_entity_, 0);
			count_			  = std::exchange(other.count_, 0);
			refresh_required_ = std::exchange(other.refresh_required_, false);
			entities_		  = std::exchange(other.entities_, {});
			refresh_		  = std::exchange(other.refresh_, {});
			versions_		  = std::exchange(other.versions_, {});
			free_entities_	  = std::exchange(other.free_entities_, {});
			pools_			  = std::exchange(other.pools_, {});
		}
		return *this;
	}

	virtual ~Manager() {
		Reset();
	}

	/**
	 * @brief Equality operator for comparing two Manager objects.
	 * @param a The first Manager to compare.
	 * @param b The second Manager to compare.
	 * @return True if the Managers are the same, false otherwise.
	 */
	friend bool operator==(const Manager& a, const Manager& b) {
		return &a == &b;
	}

	/**
	 * @brief Inequality operator for comparing two Manager objects.
	 * @param a The first Manager to compare.
	 * @param b The second Manager to compare.
	 * @return True if the Managers are different, false otherwise.
	 */
	friend bool operator!=(const Manager& a, const Manager& b) {
		return !operator==(a, b);
	}

	/**
	 * @brief Adds a construct hook for the specified component type.
	 *
	 * This hook is invoked whenever a component of type `TComponent` is constructed.
	 * Note: Discarding the returned hook instance will make it impossible to remove the hook later.
	 *
	 * @tparam TComponent The component type to attach the construct hook to.
	 * @return Reference to the newly added hook, which can be configured or stored for later
	 * removal.
	 */
	template <typename TComponent>
	[[nodiscard]] Hook<void, EntityHandle<TArchiver>>& OnConstruct() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::construct_hooks.AddHook();
	}

	/**
	 * @brief Adds a destruct hook for the specified component type.
	 *
	 * This hook is invoked whenever a component of type `TComponent` is destroyed.
	 * Note: Discarding the returned hook instance will make it impossible to remove the hook later.
	 *
	 * @tparam TComponent The component type to attach the destruct hook to.
	 * @return Reference to the newly added hook, which can be configured or stored for later
	 * removal.
	 */
	template <typename TComponent>
	[[nodiscard]] Hook<void, EntityHandle<TArchiver>>& OnDestruct() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::destruct_hooks.AddHook();
	}

	/**
	 * @brief Adds an update hook for the specified component type.
	 *
	 * This hook is invoked during update operations on a component of type `TComponent`.
	 * Note: Discarding the returned hook instance will make it impossible to remove the hook later.
	 *
	 * @tparam TComponent The component type to attach the update hook to.
	 * @return Reference to the newly added hook, which can be configured or stored for later
	 * removal.
	 */
	template <typename TComponent>
	[[nodiscard]] Hook<void, EntityHandle<TArchiver>>& OnUpdate() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::update_hooks.AddHook();
	}

	/**
	 * @brief Checks if a specific construct hook exists for the given component type.
	 *
	 * This function allows you to verify whether the provided hook is currently registered
	 * as a construct hook for the specified component type `TComponent`.
	 *
	 * @tparam TComponent The component type to check.
	 * @param hook The hook to search for in the construct hook list.
	 * @return true if the hook is registered; false otherwise.
	 */
	template <typename TComponent>
	[[nodiscard]] bool HasOnConstruct(const Hook<void, EntityHandle<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::construct_hooks.HasHook(hook);
	}

	/**
	 * @brief Checks if a specific destruct hook exists for the given component type.
	 *
	 * This function allows you to verify whether the provided hook is currently registered
	 * as a destruct hook for the specified component type `TComponent`.
	 *
	 * @tparam TComponent The component type to check.
	 * @param hook The hook to search for in the destruct hook list.
	 * @return true if the hook is registered; false otherwise.
	 */
	template <typename TComponent>
	[[nodiscard]] bool HasOnDestruct(const Hook<void, EntityHandle<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::destruct_hooks.HasHook(hook);
	}

	/**
	 * @brief Checks if a specific update hook exists for the given component type.
	 *
	 * This function allows you to verify whether the provided hook is currently registered
	 * as an update hook for the specified component type `TComponent`.
	 *
	 * @tparam TComponent The component type to check.
	 * @param hook The hook to search for in the update hook list.
	 * @return true if the hook is registered; false otherwise.
	 */
	template <typename TComponent>
	[[nodiscard]] bool HasOnUpdate(const Hook<void, EntityHandle<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::update_hooks.HasHook(hook);
	}

	/**
	 * @brief Removes a previously added construct hook for the specified component type.
	 *
	 * @tparam TComponent The component type the hook was registered to.
	 * @param hook The hook instance to remove.
	 */
	template <typename TComponent>
	void RemoveOnConstruct(const Hook<void, EntityHandle<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::construct_hooks.RemoveHook(hook);
	}

	/**
	 * @brief Removes a previously added destruct hook for the specified component type.
	 *
	 * @tparam TComponent The component type the hook was registered to.
	 * @param hook The hook instance to remove.
	 */
	template <typename TComponent>
	void RemoveOnDestruct(const Hook<void, EntityHandle<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::destruct_hooks.RemoveHook(hook);
	}

	/**
	 * @brief Removes a previously added update hook for the specified component type.
	 *
	 * @tparam TComponent The component type the hook was registered to.
	 * @param hook The hook instance to remove.
	 */
	template <typename TComponent>
	void RemoveOnUpdate(const Hook<void, EntityHandle<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::update_hooks.RemoveHook(hook);
	}

	/**
	 * @brief Refreshes the state of the manager.
	 *        Cleans up entities marked for deletion and makes the manager aware of newly created
	 * entities.
	 */
	void Refresh() {
		if (!refresh_required_) {
			return;
		}
		// This must be set before refresh starts in case
		// events are called (for instance during entity deletion).
		refresh_required_ = false;
		ECS_ASSERT(
			entities_.Size() == versions_.size(),
			"Refresh failed due to varying entity vector and version vector "
			"size"
		);
		ECS_ASSERT(
			entities_.Size() == refresh_.Size(),
			"Refresh failed due to varying entity vector and refresh vector "
			"size"
		);
		ECS_ASSERT(
			next_entity_ <= entities_.Size(),
			"Next available entity must not be out of bounds of entity vector"
		);
		Id alive{ 0 };
		Id dead{ 0 };
		for (Id entity{ 0 }; entity < next_entity_; ++entity) {
			if (!refresh_[entity]) {
				continue;
			}
			// EntityHandle was marked for refresh.
			refresh_.Set(entity, false);
			if (entities_[entity]) { // Marked for deletion.
				ClearEntity(entity);
				entities_.Set(entity, false);
				++versions_[entity];
				free_entities_.emplace_back(entity);
				++dead;
			} else { // Marked for 'creation'.
				entities_.Set(entity, true);
				++alive;
			}
		}
		// Update entity count with net change.
		count_ += alive;
		count_	= count_ > dead ? count_ - dead : 0;
	}

	/**
	 * @brief Reserves memory for the specified number of entities.
	 * @param capacity The capacity to reserve.
	 */
	void Reserve(std::size_t capacity) {
		entities_.Reserve(capacity);
		refresh_.Reserve(capacity);
		versions_.reserve(capacity);
		ECS_ASSERT(
			entities_.Capacity() == refresh_.Capacity(),
			"EntityHandle and refresh vectors must have the same capacity"
		);
	}

	/**
	 * @brief Creates a new entity. Call Refresh() after using this method.
	 * @return The created EntityHandle.
	 */
	EntityHandle<TArchiver> CreateEntity();

	/**
	 * @brief Copies an entity from one Manager to another.
	 * @tparam TComponents The component types to copy.
	 * @param from The entity to copy from.
	 * @param to The entity to copy to.
	 */
	template <typename... TComponents>
	void CopyEntity(const EntityHandle<TArchiver>& from, EntityHandle<TArchiver>& to);

	/**
	 * @brief Copies an entity and returns the new entity. Call Refresh() after using this method.
	 * @tparam TComponents The component types to copy.
	 * @param from The entity to copy from.
	 * @return The copied entity.
	 */
	template <typename... TComponents>
	EntityHandle<TArchiver> CopyEntity(const EntityHandle<TArchiver>& from);

	/**
	 * @brief Retrieves all entities that have the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return A collection of entities that have the specified components.
	 */
	template <typename... TComponents>
	[[nodiscard]] ecs::ViewWith<TArchiver, true, TComponents...> EntitiesWith() const;

	/**
	 * @brief Retrieves all entities that have the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return A collection of entities that have the specified components.
	 */
	template <typename... TComponents>
	[[nodiscard]] ecs::ViewWith<TArchiver, false, TComponents...> EntitiesWith();

	/**
	 * @brief Retrieves all entities that do not have the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return A collection of entities that do not have the specified components.
	 */
	template <typename... TComponents>
	[[nodiscard]] ecs::ViewWithout<TArchiver, true, TComponents...> EntitiesWithout() const;

	/**
	 * @brief Retrieves all entities that do not have the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return A collection of entities that do not have the specified components.
	 */
	template <typename... TComponents>
	[[nodiscard]] ecs::ViewWithout<TArchiver, false, TComponents...> EntitiesWithout();

	/**
	 * @brief Retrieves all entities in the manager.
	 * @return A collection of all entities in the manager.
	 */
	[[nodiscard]] ecs::View<TArchiver, true> Entities() const;

	/**
	 * @brief Retrieves all entities in the manager.
	 * @return A collection of all entities in the manager.
	 */
	[[nodiscard]] ecs::View<TArchiver, false> Entities();

	// @return The number of active entities in the manager.
	[[nodiscard]] std::size_t Size() const {
		return count_;
	}

	// @return True if the manager has no active entities, false otherwise.
	[[nodiscard]] bool IsEmpty() const {
		return Size() == 0;
	}

	// @return The capacity of the manager's active entity storage.
	[[nodiscard]] std::size_t Capacity() const {
		return versions_.capacity();
	}

	// @brief Clears all entities and resets the manager state.
	void Clear() {
		for (auto& pool : pools_) {
			if (pool) {
				pool->InvokeDestructHooks(*this);
			}
		}

		ClearEntities();

		for (auto& pool : pools_) {
			if (pool) {
				pool->Clear(*this);
			}
		}

		count_			  = 0;
		next_entity_	  = 0;
		refresh_required_ = false;

		entities_.Clear();
		refresh_.Clear();
		versions_.clear();
		free_entities_.clear();
	}

	// @brief Resets the manager to its initial state, clearing all entities and pools. Shrinks the
	// capacity of the storage.
	void Reset() {
		for (auto& pool : pools_) {
			if (pool) {
				pool->InvokeDestructHooks(*this);
			}
		}

		ClearEntities();

		for (auto& pool : pools_) {
			if (pool) {
				pool->Reset(*this);
			}
		}

		Clear();

		pools_.clear();

		entities_.ShrinkToFit();
		refresh_.ShrinkToFit();

		versions_.shrink_to_fit();
		free_entities_.shrink_to_fit();
		pools_.shrink_to_fit();
	}

protected:
	friend struct std::hash<EntityHandle<TArchiver>>;
	template <typename TA>
	friend class EntityHandle;
	template <typename TE, typename TA, bool is_const, LoopCriterion U, typename... TCs>
	friend class View;
	template <typename TE, typename TA, bool is_const, typename... TCs>
	friend class Pools;
	template <typename TC, typename TA>
	friend class Pool;

	virtual void ClearEntities();

	/**
	 * @brief Copies an entity's components to another entity.
	 *
	 * This function copies components from one entity to another. If specific components are
	 * provided, only those components are copied. Otherwise, all components of the entity are
	 * copied.
	 *
	 * @tparam TComponents The component types to copy.
	 * @param from_id The entity ID from which to copy.
	 * @param from_version The version of the entity to copy from.
	 * @param to_id The entity ID to which to copy.
	 * @param to_version The version of the entity to copy to.
	 */
	template <typename... TComponents>
	void CopyEntity(Id from_id, Version from_version, Id to_id, Version to_version) {
		// Assertions to ensure the validity of the entity states before copying
		ECS_ASSERT(
			IsAlive(from_id, from_version),
			"Cannot copy from entity which has not been initialized from the "
			"manager"
		);
		ECS_ASSERT(
			IsAlive(to_id, to_version),
			"Cannot copy to entity which has not been initialized from the "
			"manager"
		);

		if constexpr (sizeof...(TComponents) > 0) { // Copy only specific components.
			static_assert(
				std::conjunction_v<std::is_copy_constructible<TComponents>...>,
				"Cannot copy entity with a component that is not copy constructible"
			);
			Pools<EntityHandle<TArchiver>, TArchiver, false, TComponents...> pools{
				GetPool<TComponents>(GetId<TComponents>())...
			};
			// Validate if the pools exist and contain the required components
			ECS_ASSERT(
				pools.AllExist(), "Cannot copy entity with a component that is not "
								  "even in the manager"
			);
			ECS_ASSERT(
				pools.Has(from_id), "Cannot copy entity with a component that it does not have"
			);
			pools.Copy(*this, from_id, to_id);
		} else { // Copy all components.
			// Loop through all pools and copy components
			for (auto& pool : pools_) {
				if (pool != nullptr && pool->Has(from_id)) {
					pool->Copy(*this, from_id, to_id);
				}
			}
		}
	}

	/**
	 * @brief Generates a new entity and assigns it a version.
	 *
	 * This function generates a new entity, either by picking an available one from the free entity
	 * list or by incrementing the next available entity counter. It also handles resizing the
	 * manager if the entity capacity is exceeded.
	 *
	 * @param entity The generated entity index.
	 * @param version The version assigned to the newly created entity.
	 */
	void GenerateEntity(Id& entity, Version& version) {
		entity = 0;
		// Pick entity from free list before trying to increment entity counter.
		if (!free_entities_.empty()) {
			entity = free_entities_.front();
			free_entities_.pop_front();
		} else {
			entity = next_entity_++;
		}
		// Double the size of the manager if capacity is reached.
		if (entity >= entities_.Size()) {
			if (versions_.capacity() == 0) {
				// Reserve capacity for 1 entity so that manager size will double in powers of 2.
				Reserve(1);
			}
			Resize(versions_.capacity() * 2);
		}
		// Ensure entity is within valid bounds
		ECS_ASSERT(
			entity < entities_.Size(), "Created entity is outside of manager entity vector range"
		);
		ECS_ASSERT(!entities_[entity], "Cannot create new entity from live entity");
		ECS_ASSERT(!refresh_[entity], "Cannot create new entity from refresh marked entity");
		// Mark entity for refresh.
		refresh_.Set(entity, true);
		refresh_required_ = true;
		// EntityHandle version incremented here.
		version = ++versions_[entity];
	}

	/**
	 * @brief Resizes the internal storage for entities and their components.
	 *
	 * This function adjusts the capacity of the internal storage arrays (such as `entities_`,
	 * `refresh_`, and `versions_`) to accommodate the new size.
	 *
	 * @param size The new size to resize the storage to.
	 */
	void Resize(std::size_t size) {
		// Resize internal storage arrays if the new size is larger
		if (size > entities_.Size()) {
			entities_.Resize(size, false);
			refresh_.Resize(size, false);
			versions_.resize(size, 0);
		}
		// Validate that all storage arrays have the same size
		ECS_ASSERT(
			entities_.Size() == versions_.size(),
			"Resize failed due to varying entity vector and version vector size"
		);
		ECS_ASSERT(
			entities_.Size() == refresh_.Size(),
			"Resize failed due to varying entity vector and refresh vector size"
		);
	}

	/**
	 * @brief Clears an entity's components from the pools.
	 *
	 * This function removes all components associated with an entity from the component pools.
	 *
	 * @param entity The entity whose components are to be cleared.
	 */
	void ClearEntity(Id entity) const {
		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Remove(*this, entity);
			}
		}
	}

	/**
	 * @brief Checks if an entity is alive based on its version and state.
	 *
	 * This function determines whether an entity is alive by checking its version and whether it is
	 * in the process of creation or deletion.
	 *
	 * @param entity The entity to check.
	 * @param version The version of the entity to check.
	 * @return True if the entity is alive, otherwise false.
	 */
	[[nodiscard]] bool IsAlive(Id entity, Version version) const {
		return version != 0 && entity < versions_.size() && versions_[entity] == version &&
			   entity < entities_.Size() &&
			   // EntityHandle considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (entities_[entity] || refresh_[entity]);
	}

	/**
	 * @brief Checks if an entity is activated (i.e., exists and is marked as active).
	 *
	 * @param entity The entity to check.
	 * @return True if the entity is activated, otherwise false.
	 */
	[[nodiscard]] bool IsActivated(Id entity) const {
		return entity < entities_.Size() && entities_[entity];
	}

	[[nodiscard]] Version GetVersion(Id entity) const {
		ECS_ASSERT(entity < versions_.size(), "EntityHandle does not have a valid version");
		return versions_[entity];
	}

	/**
	 * @brief Checks if two entities match in terms of components.
	 *
	 * This function checks if two entities share the same components, meaning that they have
	 * identical component states.
	 *
	 * @param entity1 The first entity to check.
	 * @param entity2 The second entity to check.
	 * @return True if the entities match, otherwise false.
	 */
	[[nodiscard]] bool Match(Id entity1, Id entity2) const {
		for (const auto& pool : pools_) {
			if (pool == nullptr) {
				continue;
			}
			bool has1{ pool->Has(entity1) };
			bool has2{ pool->Has(entity2) };
			// Check that one entity has a component while the other doesn't.
			if ((has1 || has2) && (!has1 || !has2)) {
				// Exit early if one non-matching component is found.
				return false;
			}
		}
		return true;
	}

	/**
	 * @brief Destroys an entity by marking it for deletion and clearing its components.
	 *
	 * @param entity The entity to destroy.
	 * @param version The version of the entity to destroy.
	 */
	void DestroyEntity(Id entity, Version version) {
		ECS_ASSERT(entity < versions_.size(), "");
		ECS_ASSERT(entity < refresh_.Size(), "");
		if (versions_[entity] != version) {
			return;
		}
		if (entities_[entity]) {
			refresh_.Set(entity, true);
			refresh_required_ = true;
		} else if (refresh_[entity]) {
			/*
			 * Edge case where entity is created and marked for deletion
			 * before a Refresh() has been called.
			 * In this case, destroy and invalidate the entity without
			 * a Refresh() call. This is equivalent to an entity which
			 * never 'officially' existed in the manager.
			 */
			ClearEntity(entity);
			refresh_.Set(entity, false);
			++versions_[entity];
			free_entities_.emplace_back(entity);
		}
	}

	/**
	 * @brief Retrieves the component pool for a given component type and index.
	 *
	 * @tparam TComponent The component type to retrieve the pool for.
	 * @param component The index of the component type to retrieve the pool for.
	 * @return The pool of the specified component type.
	 */
	template <typename TComponent>
	[[nodiscard]] const Pool<TComponent, TArchiver>* GetPool(Id component) const {
		ECS_ASSERT(component == GetId<TComponent>(), "GetPool mismatch with component id");
		if (component < pools_.size()) {
			const auto& pool{ pools_[component] };
			// This is nullptr if the pool does not exist in the manager.
			return static_cast<Pool<TComponent, TArchiver>*>(pool.get());
		}
		return nullptr;
	}

	/**
	 * @brief Retrieves the component pool for a given component type and index (non-const).
	 *
	 * @tparam TComponent The component type to retrieve the pool for.
	 * @param component The index of the component type to retrieve the pool for.
	 * @return The pool of the specified component type.
	 */
	template <typename TComponent>
	[[nodiscard]] Pool<TComponent, TArchiver>* GetPool(Id component) {
		return const_cast<Pool<TComponent, TArchiver>*>(
			std::as_const(*this).template GetPool<TComponent>(component)
		);
	}

	/**
	 * @brief Removes a component from an entity.
	 *
	 * @tparam TComponent The component type to remove.
	 * @param entity The entity from which to remove the component.
	 * @param component The index of the component to remove.
	 */
	template <typename TComponent>
	void Remove(Id entity, Id component) {
		auto pool{ GetPool<TComponent>(component) };
		if (pool != nullptr) {
			pool->template Pool<TComponent, TArchiver>::Remove(*this, entity);
		}
	}

	/**
	 * @brief Retrieves the components of an entity.
	 *
	 * This function retrieves the components associated with an entity and returns them for further
	 * use.
	 *
	 * @tparam TComponents The component types to retrieve.
	 * @param entity The entity to retrieve components from.
	 * @return The components associated with the entity.
	 */
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get(Id entity) const {
		Pools<EntityHandle<TArchiver>, TArchiver, false, TComponents...> p{
			(GetPool<TComponents>(GetId<TComponents>()))...
		};
		return p.Get(entity);
	}

	/**
	 * @brief Retrieves the components of an entity (non-const).
	 *
	 * @tparam TComponents The component types to retrieve.
	 * @param entity The entity to retrieve components from.
	 * @return The components associated with the entity.
	 */
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get(Id entity) {
		Pools<EntityHandle<TArchiver>, TArchiver, false, TComponents...> p{
			(GetPool<TComponents>(GetId<TComponents>()))...
		};
		return p.Get(entity);
	}

	/**
	 * @brief Checks if an entity has a specific component.
	 *
	 * @tparam TComponent The component type to check for.
	 * @param entity The entity to check.
	 * @param component The index of the component to check for.
	 * @return True if the entity has the component, otherwise false.
	 */
	template <typename TComponent>
	[[nodiscard]] bool Has(Id entity, Id component) const {
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	/**
	 * @brief Invokes the specified components' update hooks.
	 * @tparam TComponent The component types to update.
	 */
	template <typename TComponent>
	void Update(Id entity, Id component) const {
		const auto pool{ GetPool<TComponent>(component) };
		if (pool != nullptr) {
			pool->Update(*this, entity);
		}
	}

	// Note: For now making this const and pools_ mutable so that creating an entity view with
	// components which have not been added to the manager yet enables them to be added while
	// iterating through the entities.
	template <typename TComponent>
	Pool<TComponent, TArchiver>* GetOrAddPool(Id component) const {
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1);
		}
		auto pool{ const_cast<Manager&>(*this).GetPool<TComponent>(component) };
		if (pool == nullptr) {
			auto new_pool{ std::make_unique<Pool<TComponent, TArchiver>>() };
			pool = new_pool.get();
			ECS_ASSERT(component < pools_.size(), "Component index out of range");
			pools_[component] = std::move(new_pool);
		}
		ECS_ASSERT(pool != nullptr, "Could not create new component pool correctly");
		return pool;
	}

	/**
	 * @brief Adds a new component to an entity.
	 *
	 * This function adds a new component to the entity, initializing it with the provided
	 * arguments.
	 *
	 * @tparam TComponent The component type to add.
	 * @tparam TArgs The constructor arguments for the component.
	 * @param entity The entity to add the component to.
	 * @param component The index of the component to add.
	 * @param constructor_args The arguments used to construct the new component.
	 * @return A reference to the newly added component.
	 */
	template <typename TComponent, typename... TArgs>
	TComponent& Add(Id entity, Id component, TArgs&&... constructor_args) {
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->Add(*this, entity, std::forward<TArgs>(constructor_args)...);
	}

	/**
	 * @brief Retrieves the ID of a component type.
	 *
	 * This function returns a unique ID associated with a given component type.
	 *
	 * @tparam TComponent The component type to retrieve the ID for.
	 * @return The ID associated with the component type.
	 */
	template <typename TComponent>
	[[nodiscard]] static Id GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static Id id{ ComponentCount()++ };
		return id;
	}

	/**
	 * @brief Retrieves the count of components.
	 *
	 * This function returns the current count of components in the system.
	 *
	 * @return The current component count.
	 */
	[[nodiscard]] static Id& ComponentCount() {
		static Id id{ 0 };
		return id;
	}

	// @brief Id of the next available entity.
	Id next_entity_{ 0 };

	// @brief The total count of active entities.
	Id count_{ 0 };

	// @brief Flag indicating if a refresh is required.
	bool refresh_required_{ false };

	// @brief Dynamic bitset tracking the state of entities (alive or dead).
	DynamicBitset entities_;

	// @brief Dynamic bitset used to track entities that need refreshing.
	DynamicBitset refresh_;

	// @brief Version vector for entities.
	std::vector<Version> versions_;

	// @brief Deque of free entity indices.
	std::deque<Id> free_entities_;

	// @brief Pools of component data for entities.
	// mutable because EntitiesWith may expand this with empty component pools while remaining
	// const.
	mutable std::vector<std::unique_ptr<AbstractPool<TArchiver>>> pools_;
};

/**
 * @class EntityHandle
 * @brief A class representing an entity in the ECS (EntityHandle-Component-System) pattern.
 *
 * The EntityHandle class encapsulates an entity's ID, version, and its associated manager.
 * It provides functions for adding, removing, and checking components as well as copying,
 * destroying, and comparing entities.
 */
template <typename TArchiver>
class EntityHandle {
public:
	EntityHandle() = default;

	EntityHandle(const EntityHandle&) = default;

	EntityHandle& operator=(const EntityHandle&) = default;

	EntityHandle(EntityHandle&& other) noexcept :
		entity_{ std::exchange(other.entity_, 0) },
		version_{ std::exchange(other.version_, 0) },
		manager_{ std::exchange(other.manager_, nullptr) } {}

	EntityHandle& operator=(EntityHandle&& other) noexcept {
		if (this != &other) {
			entity_	 = std::exchange(other.entity_, 0);
			version_ = std::exchange(other.version_, 0);
			manager_ = std::exchange(other.manager_, nullptr);
		}
		return *this;
	}

	~EntityHandle() noexcept = default;

	// @return True if the entity is valid, false otherwise.
	explicit operator bool() const;

	// @return True if the entity ids, versions, and managers are equal. Does not compare
	// components.
	friend bool operator==(const EntityHandle& a, const EntityHandle& b) {
		return a.entity_ == b.entity_ && a.version_ == b.version_ && a.manager_ == b.manager_;
	}

	// @return True if there is a mismatch of ids, versions or managers. Does not compare
	// components.
	friend bool operator!=(const EntityHandle& a, const EntityHandle& b) {
		return !(a == b);
	}

	/**
	 * @brief Copies the current entity.
	 * If the entity is invalid, an invalid entity is returned. Refresh the manager for the entity
	 * to appear while looping through manager entities.
	 * @tparam TComponents The component types to copy.
	 * @return A new entity that is a copy of the current one.
	 */
	template <typename... TComponents>
	EntityHandle Copy() {
		if (manager_ == nullptr) {
			return {};
		}
		return manager_->template CopyEntity<TComponents...>(*this);
	}

	/**
	 * @brief Adds a component to the entity if it does not already have it, otherwise does nothing.
	 * @tparam TComponent The component type to add.
	 * @tparam TComponents The constructor arguments for the component.
	 * @param constructor_args The arguments to construct the component.
	 * @return A reference to the added or already existing component.
	 */
	template <typename TComponent, typename... TComponents>
	TComponent& TryAdd(TComponents&&... constructor_args) {
		ECS_ASSERT(manager_ != nullptr, "Cannot add component to a null entity");
		if (auto component{ manager_->template GetId<TComponent>() };
			!manager_->template Has<TComponent>(entity_, component)) {
			return manager_->template Add<TComponent>(
				entity_, component, std::forward<TComponents>(constructor_args)...
			);
		}
		return manager_->template Get<TComponent>(entity_);
	}

	/**
	 * @brief Adds a component to the entity. If the entity already has the component, it is
	 * replaced.
	 * @tparam TComponent The component type to add.
	 * @tparam TComponents The constructor arguments for the component.
	 * @param constructor_args The arguments to construct the component.
	 * @return A reference to the added component.
	 */
	template <typename TComponent, typename... TComponents>
	TComponent& Add(TComponents&&... constructor_args) {
		ECS_ASSERT(manager_ != nullptr, "Cannot add component to a null entity");
		return manager_->template Add<TComponent>(
			entity_, manager_->template GetId<TComponent>(),
			std::forward<TComponents>(constructor_args)...
		);
	}

	/**
	 * @brief Removes components from the entity. If the entity does not have the component, does
	 * nothing.
	 * @tparam TComponents The component types to remove.
	 */
	template <typename... TComponents>
	void Remove() {
		if (manager_ == nullptr) {
			return;
		}
		(manager_->template Remove<TComponents>(entity_, manager_->template GetId<TComponents>()),
		 ...);
	}

	/**
	 * @brief Checks if the entity has all the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return True if the entity has all specified components, false otherwise.
	 */
	template <typename... TComponents>
	[[nodiscard]] bool Has() const {
		return manager_ != nullptr && (manager_->template Has<TComponents>(
										   entity_, manager_->template GetId<TComponents>()
									   ) &&
									   ...);
	}

	/**
	 * @brief Checks if the entity has any of the specified components.
	 * @tparam TComponents The component types to check for.
	 * @return True if the entity has any of the specified components, false otherwise.
	 */
	template <typename... TComponents>
	[[nodiscard]] bool HasAny() const {
		return manager_ != nullptr && (manager_->template Has<TComponents>(
										   entity_, manager_->template GetId<TComponents>()
									   ) ||
									   ...);
	}

	/**
	 * @brief Retrieves the components of the entity.
	 * @tparam TComponents The component types to retrieve.
	 * @return The components of the entity.
	 */
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		return manager_->template Get<TComponents...>(entity_);
	}

	/**
	 * @brief Retrieves the components of the entity.
	 * @tparam TComponents The component types to retrieve.
	 * @return The components of the entity.
	 */
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		return manager_->template Get<TComponents...>(entity_);
	}

	/**
	 * @brief Retrieve a const pointer to the specified entity component. If entity does not have
	 * the component, returns nullptr.
	 * @tparam TComponent The component type to retrieve.
	 * @return A const pointer component of the entity, or nullptr if the entity has no such
	 * component.
	 */
	template <typename TComponent>
	[[nodiscard]] const TComponent* TryGet() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		if (auto component{ manager_->template GetId<TComponent>() };
			manager_->template Has<TComponent>(entity_, component)) {
			return &manager_->template Get<TComponent>(entity_);
		}
		return nullptr;
	}

	/**
	 * @brief Retrieve a pointer to the specified entity component. If entity does not have the
	 * component, returns nullptr.
	 * @tparam TComponent The component type to retrieve.
	 * @return A pointer component of the entity, or nullptr if the entity has no such component.
	 */
	template <typename TComponent>
	[[nodiscard]] TComponent* TryGet() {
		return const_cast<TComponent*>(std::as_const(*this).template TryGet<TComponent>());
	}

	/**
	 * @brief Invokes the specified components' update hooks.
	 * @tparam TComponents The component types to update.
	 */
	template <typename... TComponents>
	void Update() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot update the component of a null entity");
		(manager_->template Update<TComponents>(entity_, manager_->template GetId<TComponents>()),
		 ...);
	}

	/**
	 * @brief Clears the entity's components.
	 */
	void Clear() const {
		if (manager_ == nullptr) {
			return;
		}
		manager_->ClearEntity(entity_);
	}

	/**
	 * @brief Checks if the entity is alive, i.e., if the manager has been refreshed after its
	 * creation.
	 * @return True if the entity is alive, false otherwise.
	 */
	[[nodiscard]] bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	/**
	 * @brief Destroys the entity, flagging it to be removed from the manager after the next
	 * Manager::Refresh() call.
	 * @return *this. Allows for destroying an entity and invalidating its handle in one line:
	 * handle.Destroy() = {};
	 */
	EntityHandle& Destroy() {
		if (manager_ != nullptr && manager_->IsAlive(entity_, version_)) {
			manager_->DestroyEntity(entity_, version_);
		}
		return *this;
	}

	/**
	 * @brief Gets the manager associated with the entity.
	 * @return A reference to the entity's manager.
	 */
	[[nodiscard]] Manager<TArchiver>& GetManager() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of a null entity");
		return *manager_;
	}

	/**
	 * @brief Gets the manager associated with the entity.
	 * @return A const reference to the entity's manager.
	 */
	[[nodiscard]] const Manager<TArchiver>& GetManager() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of a null entity");
		return *manager_;
	}

	/**
	 * @brief Compares the components of two entities to determine if they are identical.
	 * @param e The entity to compare to.
	 * @return True if the entities are identical, false otherwise.
	 */
	[[nodiscard]] bool IsIdenticalTo(const EntityHandle& e) const {
		if (*this == e) {
			return true;
		}

		return entity_ != e.entity_ && manager_ == e.manager_ && manager_ != nullptr
				 ? manager_->Match(entity_, e.entity_)
				 : true;
	}

	/**
	 * @brief Retrieves the internal ID/index of the entity.
	 *
	 * This ID uniquely identifies the entity within the ECS system's storage.
	 * It is typically used internally for lookups or comparisons.
	 * Note: This should not be used for serialization.
	 *
	 * @return The entity's index (ID).
	 */
	[[nodiscard]] Id GetId() const {
		return entity_;
	}

	/**
	 * @brief Retrieves the version of the entity.
	 *
	 * The version distinguishes between different incarnations of an entity
	 * that may reuse the same ID after destruction. Useful for validating handles.
	 *
	 * @return The current version of the entity.
	 */
	[[nodiscard]] Id GetVersion() const {
		return version_;
	}

protected:
	template <typename TA>
	friend class Manager;
	friend struct std::hash<EntityHandle>;
	template <typename TE, typename TA, bool is_const, LoopCriterion U, typename... TCs>
	friend class View;
	template <typename TE, typename TA, bool is_const, typename... TCs>
	friend class Pools;
	template <typename TC, typename TA>
	friend class Pool;

	/**
	 * @brief Constructs an entity with a given ID, version, and associated manager.
	 * @param entity The entity's ID.
	 * @param version The entity's version.
	 * @param manager The manager that owns the entity.
	 */
	EntityHandle(Id entity, Version version, const Manager<TArchiver>* manager) :
		entity_{ entity },
		version_{ version },
		manager_{ const_cast<Manager<TArchiver>*>(manager) } {}

	/**
	 * @brief Constructs an entity with a given ID, version, and associated manager.
	 * @param entity The entity's ID.
	 * @param version The entity's version.
	 * @param manager The manager that owns the entity.
	 */
	EntityHandle(Id entity, Version version, Manager<TArchiver>* manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	// @brief The entity's ID.
	Id entity_{ 0 };

	// @brief The entity's version.
	Version version_{ 0 };

	// @brief The manager that owns the entity.
	Manager<TArchiver>* manager_{ nullptr };
};

template <typename TArchiver>
inline EntityHandle<TArchiver>::operator bool() const {
	return *this != EntityHandle<TArchiver>{};
}

template <LoopCriterion Criterion, typename TView, typename... TComponents>
class ViewIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	using difference_type	= std::ptrdiff_t;
	using pointer			= Id;
	// using value_type		= std::tuple<EntityHandle, TComponents...> || EntityHandle;
	// using reference			= std::tuple<EntityHandle, TComponents&...>|| EntityHandle;

	/**
	 * @brief Default constructor for the iterator.
	 */
	ViewIterator() = default;

	/**
	 * @brief Assignment operator for setting the entity index.
	 * @param entity The entity index to assign to the iterator.
	 * @return A reference to the current iterator.
	 */
	ViewIterator& operator=(pointer entity) {
		entity_ = entity;
		return *this;
	}

	/**
	 * @brief Equality comparison operator for two iterators.
	 * @param a The first iterator.
	 * @param b The second iterator.
	 * @return True if the iterators point to the same entity, false otherwise.
	 */
	friend bool operator==(const ViewIterator& a, const ViewIterator& b) {
		return a.entity_ == b.entity_;
	}

	/**
	 * @brief Inequality comparison operator for two iterators.
	 * @param a The first iterator.
	 * @param b The second iterator.
	 * @return True if the iterators point to different entities, false otherwise.
	 */
	friend bool operator!=(const ViewIterator& a, const ViewIterator& b) {
		return !(a == b);
	}

	/**
	 * @brief Advances the iterator by the specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A reference to the current iterator after moving.
	 */
	ViewIterator& operator+=(const difference_type& movement) {
		entity_ += movement;
		return *this;
	}

	/**
	 * @brief Moves the iterator backwards by the specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A reference to the current iterator after moving.
	 */
	ViewIterator& operator-=(const difference_type& movement) {
		entity_ -= movement;
		return *this;
	}

	/**
	 * @brief Pre-increment operator for the iterator.
	 * @return A reference to the incremented iterator.
	 */
	ViewIterator& operator++() {
		do {
			entity_++;
		} while (ShouldIncrement());
		return *this;
	}

	/**
	 * @brief Post-increment operator for the iterator.
	 * @return A temporary copy of the iterator before incrementing.
	 */
	ViewIterator operator++(int) {
		auto temp(*this);
		++(*this);
		return temp;
	}

	/**
	 * @brief Addition operator to advance the iterator by a specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A new iterator advanced by the given steps.
	 */
	ViewIterator operator+(const difference_type& movement) {
		auto old  = entity_;
		entity_	 += movement;
		auto temp(*this);
		entity_ = old;
		return temp;
	}

	/**
	 * @brief Dereference operator for accessing the component tuple of the current entity.
	 * @return The component tuple of the current entity.
	 */
	decltype(auto) operator*() const {
		return view_.GetComponentTuple(entity_);
	}

	/**
	 * @brief Member access operator for retrieving the entity index.
	 * @return The entity index.
	 */
	pointer operator->() const {
		ECS_ASSERT(view_.EntityMeetsCriteria(entity_), "No entity with given components");
		ECS_ASSERT(view_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(!view_.IsMaxEntity(entity_), "Cannot dereference entity view iterator end");
		return entity_;
	}

	/**
	 * @brief Retrieves the entity index associated with the iterator.
	 * @return The entity index.
	 */
	pointer GetEntityId() const {
		ECS_ASSERT(view_.EntityMeetsCriteria(entity_), "No entity with given components");
		ECS_ASSERT(view_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(!view_.IsMaxEntity(entity_), "Cannot dereference entity view iterator end");
		return entity_;
	}

private:
	/**
	 * @brief Helper function to determine whether the iterator should be incremented.
	 * @return True if the iterator should be incremented, false otherwise.
	 */
	[[nodiscard]] bool ShouldIncrement() const {
		return view_.EntityWithinLimit(entity_) && !view_.EntityMeetsCriteria(entity_);
	}

	/**
	 * @brief Constructor that initializes the iterator with the given entity index and view.
	 * @param entity The entity index to initialize the iterator with.
	 * @param view The view associated with the iterator.
	 */
	ViewIterator(Id entity, TView view) : entity_(entity), view_{ view } {
		if (ShouldIncrement()) {
			this->operator++();
		}
		if (!view_.IsMaxEntity(entity_)) {
			ECS_ASSERT(
				view_.EntityWithinLimit(entity_),
				"Cannot create entity view iterator with out-of-range entity "
				"index"
			);
		}
	}

private:
	template <typename TE, typename TA, bool is_const, LoopCriterion U, typename... TCs>
	friend class View;

	// @brief The current entity index.
	Id entity_{ 0 };

	// @brief The view associated with the iterator.
	TView view_;
};

/**
 * @brief View provides iteration and access utilities for ECS entities
 * with optional filtering criteria and component access.
 *
 * @tparam TEntityHandle The entity handle type.
 * @tparam is_const Whether this view provides const access.
 * @tparam Criterion Filtering criteria for included entities.
 * @tparam TComponents Types of the components accessed through this view.
 */
template <
	typename TEntityHandle, typename TArchiver, bool is_const, LoopCriterion Criterion,
	typename... TComponents>
class View {
public:
	using ManagerType =
		std::conditional_t<is_const, const Manager<TArchiver>*, Manager<TArchiver>*>;

	View() = default;

	View(
		ManagerType manager, Id max_entity,
		const Pools<TEntityHandle, TArchiver, is_const, TComponents...>& pools
	) :
		manager_{ manager }, max_entity_{ max_entity }, pools_{ pools } {}

	using iterator = ViewIterator<
		Criterion, View<TEntityHandle, TArchiver, is_const, Criterion, TComponents...>&,
		TComponents...>;

	using const_iterator = ViewIterator<
		Criterion, const View<TEntityHandle, TArchiver, is_const, Criterion, TComponents...>&,
		TComponents...>;

	/** @brief Returns iterator to beginning */
	iterator begin() {
		return { 0, *this };
	}

	/** @brief Returns iterator to end */
	iterator end() {
		return { max_entity_, *this };
	}

	/** @brief Returns const iterator to beginning */
	const_iterator begin() const {
		return { 0, *this };
	}

	/** @brief Returns const iterator to end */
	const_iterator end() const {
		return { max_entity_, *this };
	}

	/** @brief Returns const iterator to beginning */
	const_iterator cbegin() const {
		return { 0, *this };
	}

	/** @brief Returns const iterator to end */
	const_iterator cend() const {
		return { max_entity_, *this };
	}

	/**
	 * @brief Invokes a function on each matching entity and its read-only components.
	 * @tparam IS_CONST Always true; ensures this is only instantiated for const views.
	 * @param func Function to apply to each entity and its components.
	 * func(Entity, const Component&, ...)
	 */
	template <typename F, bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	void operator()(F&& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	/**
	 * @brief Invokes a function on each matching entity and its mutable components.
	 * @tparam IS_CONST Always false; ensures this is only instantiated for mutable views.
	 * @param func Function to apply to each entity and its components.
	 * func(Entity, Component&, ...)
	 */
	template <typename F, bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	void operator()(F&& func) {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	/**
	 * @brief Applies a function to each matching entity.
	 * @param func Function to apply to each entity.
	 * func(Entity)
	 */
	template <typename F>
	void ForEach(F&& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::invoke(func, GetEntity(it.GetEntityId()));
		}
	}

	/**
	 * @brief Returns a vector of all matching entities.
	 * @return A vector containing all entities matching the filtering criteria.
	 */
	[[nodiscard]] std::vector<TEntityHandle> GetVector() const {
		std::vector<TEntityHandle> v;
		v.reserve(max_entity_);
		ForEach([&](auto e) { v.push_back(e); });
		v.shrink_to_fit();
		return v;
	}

	/**
	 * @brief Counts the number of entities that meet the filtering criteria.
	 * @return Number of matching entities.
	 */
	[[nodiscard]] std::size_t Count() const {
		std::size_t count{ 0 };
		ForEach([&]([[maybe_unused]] auto e) { ++count; });
		return count;
	}

private:
	template <typename TA>
	friend class Manager;
	template <LoopCriterion U, typename TView, typename... TCs>
	friend class ViewIterator;

	/** @brief Retrieves an entity object given its index. */
	TEntityHandle GetEntity(Id entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity view iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		return TEntityHandle{ entity, manager_->GetVersion(entity), manager_ };
	}

	/** @brief Determines whether the entity meets the loop criterion. */
	[[nodiscard]] bool EntityMeetsCriteria(Id entity) const {
		bool activated{ manager_->IsActivated(entity) };
		if (!activated) {
			return false;
		}
		if constexpr (Criterion == LoopCriterion::None) {
			return true;
		} else {
			if constexpr (Criterion == LoopCriterion::WithComponents) {
				return pools_.Has(entity);
			}
			if constexpr (Criterion == LoopCriterion::WithoutComponents) {
				return pools_.NotHas(entity);
			}
		}
	}

	/** @brief Checks if the entity index equals the maximum. */
	[[nodiscard]] bool IsMaxEntity(Id entity) const {
		return entity == max_entity_;
	}

	/** @brief Checks if the entity index is within valid range. */
	[[nodiscard]] bool EntityWithinLimit(Id entity) const {
		return entity < max_entity_;
	}

	/**
	 * @brief Retrieves a tuple of entity and const references to its components.
	 * @param entity The index of the entity.
	 * @return A tuple containing the entity and its components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(Id entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity view iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == LoopCriterion::WithComponents) {
			return pools_.GetWithEntity(entity, manager_);
		} else {
			return TEntityHandle{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	/**
	 * @brief Retrieves a tuple of entity and references to its components.
	 * @param entity The index of the entity.
	 * @return A tuple containing the entity and its mutable components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(Id entity) {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity view iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == LoopCriterion::WithComponents) {
			return pools_.GetWithEntity(entity, manager_);
		} else {
			return TEntityHandle{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	// @brief Pointer to the ECS manager.
	ManagerType manager_{ nullptr };

	// @brief Maximum valid entity index.
	Id max_entity_{ 0 };

	// @brief Pools of components managed by this view.
	Pools<TEntityHandle, TArchiver, is_const, TComponents...> pools_;
};

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::InvokeDestructHooks(const Manager<TArchiver>& manager) {
	for (auto entity : dense) {
		destruct_hooks.Invoke(EntityHandle{ entity, manager.GetVersion(entity), &manager });
	}
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Copy(
	const Manager<TArchiver>& manager, Id from_entity, Id to_entity
) {
	// Same reason as given in Clone() for why no static_assert.
	if constexpr (std::is_copy_constructible_v<TComponent>) {
		ECS_ASSERT(
			Has(from_entity), "Cannot copy from an entity which does not exist in the manager"
		);
		if (!Has(to_entity)) {
			Add(manager, to_entity, components[sparse[from_entity]]);
		} else {
			components.emplace(
				components.begin() + sparse[to_entity], components[sparse[from_entity]]
			);
			update_hooks.Invoke(EntityHandle{ to_entity, manager.GetVersion(to_entity), &manager });
		}
	} else {
		ECS_ASSERT(false, "Cannot copy an entity with a non copy constructible component");
	}
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Clear(const Manager<TArchiver>& manager) {
	InvokeDestructHooks(manager);
	components.clear();
	dense.clear();
	sparse.clear();
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Update(const Manager<TArchiver>& manager, Id entity) const {
	ECS_ASSERT(Has(entity), "Cannot update a component which the entity does not have");
	update_hooks.Invoke(EntityHandle{ entity, manager.GetVersion(entity), &manager });
}

template <typename TComponent, typename TArchiver>
bool Pool<TComponent, TArchiver>::Remove(const Manager<TArchiver>& manager, Id entity) {
	if (!Has(entity)) {
		return false;
	}
	destruct_hooks.Invoke(EntityHandle{ entity, manager.GetVersion(entity), &manager });

	// See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for
	// in-depth explanation. In short, swap with back and pop back,
	// relinking sparse ids after.
	auto last{ dense.back() };
	std::swap(dense.back(), dense[sparse[entity]]);
	std::swap(components.back(), components[sparse[entity]]);
	ECS_ASSERT(last < sparse.size(), "");
	std::swap(sparse[last], sparse[entity]);
	dense.pop_back();
	components.pop_back();
	return true;
}

template <typename TComponent, typename TArchiver>
template <typename... TArgs>
TComponent& Pool<TComponent, TArchiver>::Add(
	const Manager<TArchiver>& manager, Id entity, TArgs&&... constructor_args
) {
	static_assert(
		std::is_constructible_v<TComponent, TArgs...> ||
			tt::is_aggregate_initializable_v<TComponent, TArgs...>,
		"Cannot add component which is not constructible from given arguments"
	);
	if (entity < sparse.size()) {
		// EntityHandle has had the component before.
		if (sparse[entity] < dense.size() && dense[sparse[entity]] == entity) {
			// EntityHandle currently has the component.
			// Replace the current component with a new component.
			TComponent& component{ components[sparse[entity]] };
			component.~TComponent();
			// This approach prevents the creation of a temporary component object.
			if constexpr (std::is_aggregate_v<TComponent>) {
				new (&component) TComponent{ std::forward<TArgs>(constructor_args)... };
			} else {
				new (&component) TComponent(std::forward<TArgs>(constructor_args)...);
			}
			update_hooks.Invoke(EntityHandle{ entity, manager.GetVersion(entity), &manager });
			return component;
		}
		// EntityHandle currently does not have the component.
		sparse[entity] = static_cast<Id>(dense.size());
	} else {
		// EntityHandle has never had the component.
		sparse.resize(static_cast<std::size_t>(entity) + 1, static_cast<Id>(dense.size()));
	}
	// Add new component to the entity.
	dense.push_back(entity);

	TComponent* component{ nullptr };

	if constexpr (std::is_aggregate_v<TComponent>) {
		component = &components.emplace_back(std::move(TComponent{
			std::forward<TArgs>(constructor_args)... }));
	} else {
		component = &components.emplace_back(std::forward<TArgs>(constructor_args)...);
	}

	construct_hooks.Invoke(EntityHandle{ entity, manager.GetVersion(entity), &manager });
	return *component;
}

template <typename TEntityHandle, typename TArchiver, bool is_const, typename... TComponents>
[[nodiscard]] constexpr decltype(auto)
Pools<TEntityHandle, TArchiver, is_const, TComponents...>::GetWithEntity(
	Id entity, const Manager<TArchiver>* manager
) const {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(TComponents) > 0);
	return std::tuple<TEntityHandle, const TComponents&...>(
		TEntityHandle{ entity, manager->GetVersion(entity), manager },
		(GetPool<TComponents>()->template Pool<TComponents, TArchiver>::Get(entity))...
	);
}

template <typename TEntityHandle, typename TArchiver, bool is_const, typename... TComponents>
[[nodiscard]] constexpr decltype(auto)
Pools<TEntityHandle, TArchiver, is_const, TComponents...>::GetWithEntity(
	Id entity, Manager<TArchiver>* manager
) {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(TComponents) > 0);
	return std::tuple<TEntityHandle, TComponents&...>(
		TEntityHandle{ entity, manager->GetVersion(entity), manager },
		(GetPool<TComponents>()->template Pool<TComponents, TArchiver>::Get(entity))...
	);
}

template <typename TArchiver>
inline EntityHandle<TArchiver> Manager<TArchiver>::CreateEntity() {
	Id entity{ 0 };
	Version version{ 0 };
	GenerateEntity(entity, version);
	ECS_ASSERT(version != 0, "Failed to create new entity in manager");
	return EntityHandle{ entity, version, this };
}

template <typename TArchiver>
template <typename... TComponents>
inline void Manager<TArchiver>::CopyEntity(
	const EntityHandle<TArchiver>& from, EntityHandle<TArchiver>& to
) {
	CopyEntity<TComponents...>(from.entity_, from.version_, to.entity_, to.version_);
}

template <typename TArchiver>
template <typename... TComponents>
inline EntityHandle<TArchiver> Manager<TArchiver>::CopyEntity(const EntityHandle<TArchiver>& from) {
	EntityHandle<TArchiver> to{ CreateEntity() };
	CopyEntity<TComponents...>(from, to);
	return to;
}

template <typename TArchiver>
template <typename... TComponents>
inline ecs::ViewWith<TArchiver, true, TComponents...> Manager<TArchiver>::EntitiesWith() const {
	return { this, next_entity_,
			 Pools<EntityHandle<TArchiver>, TArchiver, true, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline ecs::ViewWith<TArchiver, false, TComponents...> Manager<TArchiver>::EntitiesWith() {
	return { this, next_entity_,
			 Pools<EntityHandle<TArchiver>, TArchiver, false, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline ecs::ViewWithout<TArchiver, true, TComponents...> Manager<TArchiver>::EntitiesWithout(
) const {
	return { this, next_entity_,
			 Pools<EntityHandle<TArchiver>, TArchiver, true, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline ecs::ViewWithout<TArchiver, false, TComponents...> Manager<TArchiver>::EntitiesWithout() {
	return { this, next_entity_,
			 Pools<EntityHandle<TArchiver>, TArchiver, false, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
inline ecs::View<TArchiver, true> Manager<TArchiver>::Entities() const {
	return { this, next_entity_, Pools<EntityHandle<TArchiver>, TArchiver, true>{} };
}

template <typename TArchiver>
inline ecs::View<TArchiver, false> Manager<TArchiver>::Entities() {
	return { this, next_entity_, Pools<EntityHandle<TArchiver>, TArchiver, false>{} };
}

template <typename TArchiver>
inline void Manager<TArchiver>::ClearEntities() {
	for (auto entity : Entities()) {
		entity.Destroy();
	}
}

} // namespace impl

using Entity  = ecs::impl::EntityHandle<impl::VoidArchiver>;
using Manager = ecs::impl::Manager<impl::VoidArchiver>;

} // namespace ecs

namespace std {

template <typename TArchiver>
struct hash<ecs::impl::EntityHandle<TArchiver>> {
	size_t operator()(const ecs::impl::EntityHandle<TArchiver>& e) const {
		// Source: https://stackoverflow.com/a/17017281
		size_t h{ 17 };
		h = h * 31 + hash<ecs::impl::Manager<TArchiver>*>()(e.manager_
					 ); /**< Hash for the associated manager pointer. */
		h = h * 31 + hash<ecs::impl::Id>()(e.entity_); /**< Hash for the entity's unique index. */
		h = h * 31 +
			hash<ecs::impl::Version>()(e.version_);	   /**< Hash for the entity's version number. */
		return h;									   /**< Final combined hash value. */
	}
};

} // namespace std