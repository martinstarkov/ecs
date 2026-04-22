/// MIT License
///
/// Copyright (c) 2026 | Martin Starkov | https://github.com/martinstarkov
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy
/// of this software and associated documentation files (the "Software"), to deal
/// in the Software without restriction, including without limitation the rights
/// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
/// copies of the Software, and to permit persons to whom the Software is
/// furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in all
/// copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
/// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
/// SOFTWARE.

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

#include <cstdio>
#include <cstdlib>
#include <cstring>

/// @brief Macro to trigger a debug break for debugging purposes.
#define ECS_DEBUGBREAK() ((void)0)

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() __debugbreak()
#elif defined(__linux__)
#include <signal.h>
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() raise(SIGTRAP)
#endif

/// @param condition Condition to check.
/// @param message Message shown if the condition fails (prints location, breaks, aborts).
#define ECS_ASSERT(condition, message)                                                     \
	{                                                                                      \
		if (!(condition)) {                                                                \
			std::fprintf(                                                                  \
				stderr, "ECS ASSERTION FAILED: %s:%d: %s\n", __FILE__, __LINE__, (message) \
			);                                                                             \
			ECS_DEBUGBREAK();                                                              \
			std::abort();                                                                  \
		}                                                                                  \
	}

#else

#define ECS_ASSERT(...) ((void)0)

#endif

namespace ecs {

namespace impl {

template <typename TArchiver>
class BaseEntity;

template <typename TArchiver>
class BaseManager;

using Id	  = std::uint32_t;
using Version = std::uint32_t;

/// @brief Enum for defining loop criteria for iterating through entities.
enum class LoopCriterion {
	None,
	WithComponents,
	WithoutComponents
};

template <typename TManager, LoopCriterion Criterion, typename... TComponents>
class View;

} // namespace impl

/// @brief A class that wraps a callable hook (either free function or member function).
template <typename TReturn, typename... TArgs>
class Hook {
public:
	/// Function type for the hook implementation.
	using FunctionType = TReturn (*)(void*, TArgs...);
	/// Return type of the hook.
	using ReturnType = TReturn;

	Hook() = default;

	/// @brief Connects a free (non-member) function to this hook.
	///
	/// @tparam Function A free function pointer of type TReturn(TArgs...).
	/// @return *this.
	template <auto Function>
	Hook& Connect() noexcept {
		fn_ = [](void*, TArgs... args) -> TReturn {
			return std::invoke(Function, std::forward<TArgs>(args)...);
		};
		instance_ = nullptr;
		return *this;
	}

	/// @brief Connects a non-capturing lambda to this hook.
	///
	/// @tparam Lambda Type of the lambda or function object.
	/// @param lambda A non-capturing lambda to connect.
	/// @return Reference to this HookImpl instance.
	///
	///@note Capturing lambdas are not supported in this overload, as they cannot
	///      be converted to function pointers in C++17.
	template <typename Lambda>
	Hook& Connect(Lambda lambda) noexcept {
		fn_		  = +lambda;
		instance_ = nullptr;
		return *this;
	}

	/// @brief Connects a member function to this hook.
	///
	/// @tparam Type The class type that contains the member function.
	/// @tparam Member A member function pointer of type TReturn(Type::*)(TArgs...).
	/// @param obj Pointer to the instance of the object.
	/// @return *this.
	template <typename Type, auto Member>
	Hook& Connect(Type* obj) noexcept {
		fn_ = [](void* instance, TArgs... args) -> TReturn {
			return (static_cast<Type*>(instance)->*Member)(std::forward<TArgs>(args)...);
		};
		instance_ = obj;
		return *this;
	}

	/// @brief Invokes the connected function with the given arguments.
	///
	/// @param args Arguments to pass to the function.
	/// @return Result of the function call.
	template <typename... THookArgs>
	TReturn operator()(THookArgs... args) const {
		return std::invoke(fn_, instance_, std::forward<THookArgs>(args)...);
	}

	friend bool operator==(const Hook& a, const Hook& b) {
		return a.fn_ == b.fn_ && a.instance_ == b.instance_;
	}

	friend bool operator!=(const Hook& a, const Hook& b) {
		return !operator==(a, b);
	}

private:
	/// @brief Function pointer with void* for object binding.
	FunctionType fn_{ nullptr };

	/// @brief Pointer to the object instance for member functions.
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

template <typename T>
struct is_tuple : std::false_type {};

template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
static constexpr bool is_tuple_v = is_tuple<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template <typename F, typename Tuple, std::size_t... Is>
static constexpr bool expand_tuple_v(std::index_sequence<Is...>) {
	return std::is_invocable_v<F, std::tuple_element_t<Is, std::remove_reference_t<Tuple>>...>;
}

template <typename F, typename Tuple>
static constexpr bool is_expanded_tuple_v = expand_tuple_v<F, Tuple>(
	std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{}
);

template <typename F, typename Arg>
decltype(auto) InvokePredicate(F&& func, Arg&& arg) {
	using ArgType = decltype(arg);

	if constexpr (is_tuple_v<ArgType>) {
		if constexpr (std::is_invocable_v<F, ArgType>) {
			// Predicate takes the tuple as a single argument
			return std::invoke(std::forward<F>(func), std::forward<Arg>(arg));
		} else if constexpr (is_expanded_tuple_v<F, ArgType>) {
			// Predicate takes (entity, components...,)
			return std::apply(
				[&func](auto&&... xs) -> decltype(auto) {
					return std::invoke(std::forward<F>(func), std::forward<decltype(xs)>(xs)...);
				},
				std::forward<Arg>(arg)
			);
		} else {
			static_assert(
				std::is_invocable_v<F, ArgType> || is_expanded_tuple_v<F, ArgType>,
				"Predicate must be invocable with either the tuple itself or its expanded contents"
			);
		}
	} else {
		static_assert(
			std::is_invocable_v<F, ArgType>, "Predicate must be invocable with the entity"
		);
		return std::invoke(std::forward<F>(func), std::forward<Arg>(arg));
	}
}

} // namespace tt

/// @brief No-op archiver. Provides empty (de)serialization methods as a placeholder when not
/// needed.
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

	// The type exists for this function so unserializable components can be skipped.
	template <typename TComponent>
	void SetDenseSet(const std::vector<Id>& dense_set);

	// The type exists for this function so unserializable components can be skipped.
	template <typename TComponent>
	void SetSparseSet(const std::vector<Id>& sparse_set);

	template <typename TComponent>
	[[nodiscard]] std::vector<TComponent> ReadComponents() const;

	// The type exists for this function so undeserializable components can be skipped.
	template <typename TComponent>
	[[nodiscard]] std::vector<Id> GetDenseSet() const;

	// The type exists for this function so undeserializable components can be skipped.
	template <typename TComponent>
	[[nodiscard]] std::vector<Id> GetSparseSet() const;
};

/// @brief Base interface for component pools. Defines common pool operations; implemented by
/// concrete component types.
template <typename TArchiver = VoidArchiver>
class AbstractPool {
public:
	virtual ~AbstractPool() = default;

	/// @return True if the components of the pool can be cloned, otherwise false.
	[[nodiscard]] virtual bool IsCloneable() const = 0;

	/// @return A unique pointer to a new instance of the pool.
	[[nodiscard]] virtual std::unique_ptr<AbstractPool> Clone() const = 0;

	// @param manager The manager which owns the pool.
	virtual void InvokeDestructHooks(const BaseManager<TArchiver>& manager) = 0;

	/// @brief Copies a component from one entity to another within the pool.
	///
	/// @param manager The manager within which the copy is occurring.
	virtual void Copy(const BaseManager<TArchiver>& manager, Id from_entity, Id to_entity) = 0;

	/// @brief Removes all components from the pool but does not reset the size of the pool.
	///
	/// @param manager The manager to which the component pool belongs.
	virtual void Clear(const BaseManager<TArchiver>& manager) = 0;

	/// @brief Clears the pool and shrinks its memory usage to fit the current size.
	///
	/// @param manager The manager to which the component pool belongs.
	virtual void Reset(const BaseManager<TArchiver>& manager) = 0;

	/// @brief Removes a component from the pool for a given entity.
	///
	/// @param manager The manager to which the entity belongs.
	/// @return True if the component was successfully removed, otherwise false.
	virtual bool Remove(const BaseManager<TArchiver>& manager, Id entity) = 0;

	/// @brief Checks if the pool contains a component for the specified entity.
	///
	/// @return True if the entity has a component in the pool, otherwise false.
	[[nodiscard]] virtual bool Has(Id entity) const = 0;

	/// @brief Calls the update hooks of the component for the specified entity.
	///
	/// @param manager The manager to which the entity belongs.
	/// @param entity The entity to update.
	virtual void Update(const BaseManager<TArchiver>& manager, Id entity) const = 0;

	/// @brief Serializes all components in the pool using the provided archiver.
	///
	/// This function should be implemented by derived classes to serialize their internal component
	/// data into the given archiver. The format and behavior of serialization depends on the
	/// specific TArchiver type.
	///
	/// @param archiver The archiver instance used to write the serialized component data.
	virtual void Serialize(TArchiver& archiver) const = 0;

	/// @brief Serializes the component associated with a specific entity using the provided
	/// archiver.
	///
	/// This function should be implemented by derived classes to serialize the internal data
	/// of a single component identified by the given entity index. The serialized output
	/// must be consistent with the format expected by the corresponding deserialization function.
	///
	/// @param archiver The archiver instance used to write the serialized component data.
	/// @param entity The index of the entity whose component should be serialized.
	virtual void Serialize(TArchiver& archiver, Id entity) const = 0;

	/// @brief Deserializes components into the pool using the provided archiver.
	///
	/// This function should be implemented by derived classes to reconstruct their internal
	/// component data from the given archiver. The format must match that used during
	/// serialization.
	///
	/// @param archiver The archiver instance used to read and load the component data.
	virtual void Deserialize(const TArchiver& archiver) = 0;

	/// @brief Deserializes component data for a specific entity from the provided archiver.
	///
	/// This function should be implemented by derived classes to reconstruct the internal
	/// component data for a single entity from the archiver. The input format must match
	/// the one used during serialization of that entity's component.
	///
	/// @param archiver The archiver instance used to read and load the component data.
	/// @param manager The manager that the entity belongs to.
	/// @param entity The index of the entity for which the component data should be deserialized.
	virtual void Deserialize(
		const TArchiver& archiver, const BaseManager<TArchiver>& manager, Id entity
	) = 0;
};

/// @brief A pool for storing and managing multiple hooks.
template <typename TReturn, typename... TArgs>
class HookPool {
public:
	/// Type alias for the hook implementation.
	using HookType = ecs::Hook<TReturn, TArgs...>;

	/// @brief Adds a new default-initialized hook to the pool.
	///
	/// @return Reference to the newly added hook.
	HookType& AddHook() {
		return hooks_.emplace_back();
	}

	/// @return True if the hook pool has the specified hook, false otherwise.
	[[nodiscard]] bool HasHook(const HookType& hook) const {
		auto it{ std::find(hooks_.begin(), hooks_.end(), hook) };
		return it != hooks_.end();
	}

	void RemoveHook(const HookType& hook) {
		auto it{ std::find(hooks_.begin(), hooks_.end(), hook) };
		ECS_ASSERT(it != hooks_.end(), "Cannot remove hook which has not been added");
		hooks_.erase(it);
	}

	/// @brief Invokes all hooks in the pool with the given arguments.
	///
	/// @param args Arguments to pass to each hook.
	void Invoke(TArgs... args) const {
		for (const auto& hook : hooks_) {
			std::invoke(hook, args...);
		}
	}

private:
	/// @brief Adds an existing hook to the pool.
	void AddHook(const HookType& hook) {
		hooks_.emplace_back(hook);
	}

	std::vector<HookType> hooks_;
};

template <typename TArchiver>
using ComponentHooks = HookPool<void, BaseEntity<TArchiver>>;

/// @brief Component pool for TComponent with optional serialization.
/// Stores and manages components efficiently, mapping them to entities.
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

	/// @brief Serializes the component data in the pool using the provided archiver.
	///
	/// @param archiver The archiver instance used to write component data.
	void Serialize(TArchiver& archiver) const override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver>) {
			archiver.template WriteComponents<TComponent>(components);
			archiver.template SetDenseSet<TComponent>(dense);
			archiver.template SetSparseSet<TComponent>(sparse);
		}
	}

	/// @brief Serializes the component data of a single entity using the provided archiver.
	///
	/// @param archiver The archiver instance used to write component data.
	/// @param entity The index of the entity whose component should be serialized.
	void Serialize(TArchiver& archiver, Id entity) const override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver>) {
			if (Has(entity)) {
				archiver.template WriteComponent<TComponent>(Get(entity));
			}
		}
	}

	/// @brief Deserializes component data into the pool using the provided archiver.
	///
	/// @param archiver The archiver instance used to read component data.
	void Deserialize(const TArchiver& archiver) override {
		if constexpr (!std::is_same_v<TArchiver, VoidArchiver> &&
					  std::is_default_constructible_v<TComponent>) {
			components = archiver.template ReadComponents<TComponent>();
			dense	   = archiver.template GetDenseSet<TComponent>();
			sparse	   = archiver.template GetSparseSet<TComponent>();
		}
	}

	/// @brief Deserializes component data for a single entity using the provided archiver.
	///
	/// @param archiver The archiver instance used to read component data.
	/// @param manager The manager that the entity belongs to.
	/// @param entity The index of the entity for which the component data should be deserialized.
	void Deserialize(const TArchiver& archiver, const BaseManager<TArchiver>& manager, Id entity)
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

	/// @brief Checks if the pool's components are cloneable.
	///
	/// @return True if the components are copy constructible, otherwise false.
	[[nodiscard]] bool IsCloneable() const override {
		return std::is_copy_constructible_v<TComponent>;
	}

	/// @brief Clones the pool and returns a new instance with the same data.
	///
	/// @return A unique pointer to a new pool instance with the same components.
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

	/// @brief Invokes destruct hooks on all the components in the pool.
	///
	/// @param manager The manager which owns the pool.
	void InvokeDestructHooks(const BaseManager<TArchiver>& manager) override;

	/// @brief Copies a component from one entity to another.
	///
	/// @param manager The manager within which the copy is occurring.
	/// @param from_entity The source entity from which to copy the component.
	/// @param to_entity The target entity to which the component will be copied.
	void Copy(const BaseManager<TArchiver>& manager, Id from_entity, Id to_entity) override;

	/// @brief Clears the pool, removing all components.
	///
	/// This method removes all components from the pool.
	///
	/// @param manager The manager to which the component pool belongs.
	void Clear(const BaseManager<TArchiver>& manager) override;

	/// @brief Resets the pool by clearing all components and shrinking memory usage.
	///
	/// @param manager The manager to which the component pool belongs.
	void Reset(const BaseManager<TArchiver>& manager) override {
		Clear(manager);

		components.shrink_to_fit();
		dense.shrink_to_fit();
		sparse.shrink_to_fit();
	}

	/// @brief Removes a component from the pool for a given entity.
	///
	/// @param manager The manager to which the entity belongs.
	/// @param entity The entity from which to remove the component.
	/// @return True if the component was successfully removed, otherwise false.
	bool Remove(const BaseManager<TArchiver>& manager, Id entity) override;

	/// @brief Checks if the pool contains a component for the specified entity.
	///
	/// @param entity The entity to check.
	/// @return True if the entity has a component in the pool, otherwise false.
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

	/// @brief Calls the update hooks of the component for the specified entity.
	/// @param manager The manager to which the entity belongs.
	/// @param entity The entity to update.
	virtual void Update(const BaseManager<TArchiver>& manager, Id entity) const override;

	/// @brief Retrieves a constant reference to the component associated with the specified entity.
	/// @param entity The entity whose component is to be retrieved.
	/// @return A constant reference to the component associated with the entity.
	[[nodiscard]] const TComponent& Get(Id entity) const {
		ECS_ASSERT(Has(entity), "BaseEntity does not have the requested component");
		ECS_ASSERT(
			sparse[entity] < components.size(),
			"Likely attempting to retrieve a component before it has been fully added"
		);
		return components[sparse[entity]];
	}

	/// @brief Retrieves a reference to the component associated with the specified entity.
	/// @param entity The entity whose component is to be retrieved.
	/// @return A reference to the component associated with the entity.
	[[nodiscard]] TComponent& Get(Id entity) {
		return const_cast<TComponent&>(std::as_const(*this).Get(entity));
	}

	/// @return Number of components in the pool.
	[[nodiscard]] std::size_t Size() const noexcept {
		return components.size();
	}

	/// @brief Adds a component to the pool for the specified entity.
	/// @param manager The manager to which the entity belongs.
	/// @param entity The entity to which the component will be added.
	/// @param constructor_args Arguments to construct the component.
	/// @return A reference to the newly added component.
	template <typename... TArgs>
	TComponent& Add(const BaseManager<TArchiver>& manager, Id entity, TArgs&&... constructor_args);

	/// @brief Dense components stored contiguously for cache efficiency.
	std::vector<TComponent> components;

	/// @brief Indices of the components.
	std::vector<Id> dense;

	/// @brief Indices of the entities in the dense set.
	std::vector<Id> sparse;

	ComponentHooks<TArchiver> construct_hooks;
	ComponentHooks<TArchiver> update_hooks;
	ComponentHooks<TArchiver> destruct_hooks;
};

/// @brief Manages multiple component pools. Provides type-safe access to pools of different
/// component types.
template <typename TEntity, typename TArchiver, typename... TComponents>
class Pools {
public:
	template <typename TComponent>
	using PoolType = Pool<TComponent, TArchiver>;

	explicit constexpr Pools(PoolType<TComponents>*... pools) : pools_{ pools... } {}

	template <typename TComponent>
	constexpr PoolType<TComponent>* GetPool() {
		return std::get<PoolType<TComponent>*>(pools_);
	}

	template <typename TComponent>
	constexpr const PoolType<TComponent>* GetPool() const {
		return std::get<PoolType<TComponent>*>(pools_);
	}

	constexpr void Copy(const BaseManager<TArchiver>& manager, Id from_id, Id to_id) {
		(GetPool<TComponents>()->Copy(manager, from_id, to_id), ...);
	}

	[[nodiscard]] constexpr bool Has(Id entity) const {
		ECS_ASSERT(
			AllExist(), "Component pools cannot be destroyed while stored in a Pools object"
		);
		return (GetPool<TComponents>()->Has(entity) && ...);
	}

	[[nodiscard]] constexpr bool NotHas(Id entity) const {
		return ((GetPool<TComponents>() == nullptr) || ...) ||
			   (!GetPool<TComponents>()->Has(entity) || ...);
	}

	[[nodiscard]] constexpr decltype(auto) GetWithEntity(
		Id entity, BaseManager<TArchiver>* manager
	);

	[[nodiscard]] constexpr decltype(auto) GetWithEntity(
		Id entity, const BaseManager<TArchiver>* manager
	) const;

	[[nodiscard]] constexpr decltype(auto) Get(Id entity) {
		ECS_ASSERT(
			AllExist(), "BaseManager does not have at least one of the requested components"
		);
		static_assert(sizeof...(TComponents) > 0);

		if constexpr (sizeof...(TComponents) == 1) {
			return (GetOne<TComponents>(entity), ...);
		} else {
			return std::forward_as_tuple(GetOne<TComponents>(entity)...);
		}
	}

	[[nodiscard]] constexpr decltype(auto) Get(Id entity) const {
		ECS_ASSERT(
			AllExist(), "BaseManager does not have at least one of the requested components"
		);
		static_assert(sizeof...(TComponents) > 0);

		if constexpr (sizeof...(TComponents) == 1) {
			return (GetOne<TComponents>(entity), ...);
		} else {
			return std::forward_as_tuple(GetOne<TComponents>(entity)...);
		}
	}

	template <typename TComponent>
	constexpr TComponent& GetOne(Id entity) {
		return GetPool<TComponent>()->Get(entity);
	}

	template <typename TComponent>
	constexpr const TComponent& GetOne(Id entity) const {
		return GetPool<TComponent>()->Get(entity);
	}

	constexpr bool AllExist() const {
		return AllPools([](auto* pool) { return pool != nullptr; });
	}

	template <typename TComponent>
	std::size_t Size() const noexcept {
		auto* pool = GetPool<TComponent>();
		return pool ? pool->Size() : 0;
	}

	template <typename F>
	constexpr void ForEachPool(F&& f) {
		(f(GetPool<TComponents>()), ...);
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
	std::tuple<PoolType<TComponents>*...> pools_;
};

/// @brief A dynamic bitset implementation that allows efficient bit-level operations.
/// This class is a modified version of the dynamic_bitset and allows manipulation of individual
/// bits, as well as resizing, clearing, and reserving storage space for the bitset.
class DynamicBitset {
	// Modified version of:
	// https://github.com/syoyo/dynamic_bitset/blob/master/dynamic_bitset.hh
public:
	DynamicBitset() = default;

	DynamicBitset(std::size_t bit_count, const std::vector<std::uint8_t>& data) :
		bit_count_{ bit_count }, data_{ data } {}

	/// @brief Sets the bit at the specified index to a given value.
	///
	/// This method modifies the bit at the specified index to either true or false.
	///
	/// @param index The index of the bit to be modified.
	/// @param value The value to set the bit to. Default is true.
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

	/// @brief Gets the value of the bit at the specified index.
	///
	/// This method returns whether the bit at the specified index is set to true (1) or false (0).
	///
	/// @param index The index of the bit to check.
	/// @return True if the bit is set, otherwise false.
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

	/// @return The number of bits in the bitset.
	[[nodiscard]] std::size_t Size() const {
		return bit_count_;
	}

	/// @return Returns the current capacity of the bitset. This is the number of bits that can be
	// stored before resizing the internal storage.
	[[nodiscard]] std::size_t Capacity() const {
		return data_.capacity();
	}

	/// @brief Reserves enough capacity to store a specific number of bits.
	///
	/// This method ensures that the internal storage is large enough to hold the specified number
	/// of bits, without reallocation happening until the bitset grows beyond that size.
	///
	/// @param new_capacity The number of bits for which to reserve capacity.
	void Reserve(std::size_t new_capacity) {
		auto byte_count{ GetByteCount(new_capacity) };
		data_.reserve(byte_count);
	}

	/// @brief Resizes the bitset to a new size, optionally initializing all bits to a value.
	///
	/// This method changes the size of the bitset, either truncating or expanding it. Optionally,
	/// the new bits can be initialized to a specific value.
	///
	/// @param new_size The new size of the bitset.
	/// @param value The value to initialize new bits to. Default is true.
	void Resize(std::size_t new_size, bool value) {
		auto byte_count{ GetByteCount(new_size) };
		bit_count_ = new_size;
		data_.resize(byte_count, value);
	}

	/// @brief Clears the bitset, resetting its size and data.
	///
	/// This method resets the size of the bitset to zero and clears the underlying storage.
	void Clear() {
		bit_count_ = 0;
		data_.clear();
	}

	/// @brief Shrinks the capacity of the bitset to fit its current size.
	///
	/// This method reduces the internal storage to the minimum required to store the current
	/// number of bits, helping to reduce memory usage.
	void ShrinkToFit() {
		data_.shrink_to_fit();
	}

	/// @brief Retrieves the bitset vector.
	///
	/// @return The internal storage vector of bytes.
	[[nodiscard]] const std::vector<std::uint8_t>& GetData() const {
		return data_;
	}

private:
	/// @brief Calculates the number of bytes needed to store a given number of bits.
	///
	/// @param bit_count The number of bits to store.
	/// @return The number of bytes required to store the specified number of bits.
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

/// @brief ECS entity manager.
/// Handles creation, deletion, refresh, and component management.
/// Provides utilities for copying, querying, and clearing entities.
/// Supports (de)serialization via TArchiver.
template <typename TArchiver>
class BaseManager {
public:
	using ArchiverType = TArchiver;

	using EntityType = BaseEntity<TArchiver>;

	template <LoopCriterion Criterion, typename... TComponents>
	using ViewType = View<BaseManager, Criterion, TComponents...>;

	template <LoopCriterion Criterion, typename... TComponents>
	using ConstViewType = View<const BaseManager, Criterion, TComponents...>;

	BaseManager() = default;

	BaseManager(const BaseManager& other) :
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

	BaseManager& operator=(const BaseManager& other) {
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

	BaseManager(BaseManager&& other) noexcept :
		next_entity_{ std::exchange(other.next_entity_, 0) },
		count_{ std::exchange(other.count_, 0) },
		refresh_required_{ std::exchange(other.refresh_required_, false) },
		entities_{ std::exchange(other.entities_, {}) },
		refresh_{ std::exchange(other.refresh_, {}) },
		versions_{ std::exchange(other.versions_, {}) },
		free_entities_{ std::exchange(other.free_entities_, {}) },
		pools_{ std::exchange(other.pools_, {}) } {}

	BaseManager& operator=(BaseManager&& other) noexcept {
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

	virtual ~BaseManager() {
		Reset();
	}

	/// @brief Equality operator for comparing two BaseManager objects.
	/// @param a The first BaseManager to compare.
	/// @param b The second BaseManager to compare.
	/// @return True if the Managers are the same, false otherwise.
	friend bool operator==(const BaseManager& a, const BaseManager& b) {
		return &a == &b;
	}

	/// @brief Inequality operator for comparing two BaseManager objects.
	/// @param a The first BaseManager to compare.
	/// @param b The second BaseManager to compare.
	/// @return True if the Managers are different, false otherwise.
	friend bool operator!=(const BaseManager& a, const BaseManager& b) {
		return !operator==(a, b);
	}

	/// @brief Adds a construct hook for the specified component type.
	///
	/// This hook is invoked whenever a component of type `TComponent` is constructed.
	/// Note: Discarding the returned hook instance will make it impossible to remove the hook
	/// later.
	///
	/// @tparam TComponent The component type to attach the construct hook to.
	/// @return Reference to the newly added hook, which can be configured or stored for later
	/// removal.
	template <typename TComponent>
	[[nodiscard]] Hook<void, BaseEntity<TArchiver>>& OnConstruct() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::construct_hooks.AddHook();
	}

	/// @brief Adds a destruct hook for the specified component type.
	///
	/// This hook is invoked whenever a component of type `TComponent` is destroyed.
	/// Note: Discarding the returned hook instance will make it impossible to remove the hook
	/// later.
	///
	/// @tparam TComponent The component type to attach the destruct hook to.
	/// @return Reference to the newly added hook, which can be configured or stored for later
	/// removal.
	template <typename TComponent>
	[[nodiscard]] Hook<void, BaseEntity<TArchiver>>& OnDestruct() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::destruct_hooks.AddHook();
	}

	/// @brief Adds an update hook for the specified component type.
	///
	/// This hook is invoked during update operations on a component of type `TComponent`.
	/// Note: Discarding the returned hook instance will make it impossible to remove the hook
	/// later.
	///
	/// @tparam TComponent The component type to attach the update hook to.
	/// @return Reference to the newly added hook, which can be configured or stored for later
	/// removal.
	template <typename TComponent>
	[[nodiscard]] Hook<void, BaseEntity<TArchiver>>& OnUpdate() {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->template Pool<TComponent, TArchiver>::update_hooks.AddHook();
	}

	/// @brief Checks if a specific construct hook exists for the given component type.
	///
	/// This function allows you to verify whether the provided hook is currently registered
	/// as a construct hook for the specified component type `TComponent`.
	///
	/// @tparam TComponent The component type to check.
	/// @param hook The hook to search for in the construct hook list.
	/// @return true if the hook is registered; false otherwise.
	template <typename TComponent>
	[[nodiscard]] bool HasOnConstruct(const Hook<void, BaseEntity<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::construct_hooks.HasHook(hook);
	}

	/// @brief Checks if a specific destruct hook exists for the given component type.
	///
	/// This function allows you to verify whether the provided hook is currently registered
	/// as a destruct hook for the specified component type `TComponent`.
	///
	/// @tparam TComponent The component type to check.
	/// @param hook The hook to search for in the destruct hook list.
	/// @return true if the hook is registered; false otherwise.
	template <typename TComponent>
	[[nodiscard]] bool HasOnDestruct(const Hook<void, BaseEntity<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::destruct_hooks.HasHook(hook);
	}

	/// @brief Checks if a specific update hook exists for the given component type.
	///
	/// This function allows you to verify whether the provided hook is currently registered
	/// as an update hook for the specified component type `TComponent`.
	///
	/// @tparam TComponent The component type to check.
	/// @param hook The hook to search for in the update hook list.
	/// @return true if the hook is registered; false otherwise.
	template <typename TComponent>
	[[nodiscard]] bool HasOnUpdate(const Hook<void, BaseEntity<TArchiver>>& hook) const {
		auto component{ GetId<TComponent>() };
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr &&
			   pool->template Pool<TComponent, TArchiver>::update_hooks.HasHook(hook);
	}

	/// @brief Removes a previously added construct hook for the specified component type.
	///
	/// @tparam TComponent The component type the hook was registered to.
	/// @param hook The hook instance to remove.
	template <typename TComponent>
	void RemoveOnConstruct(const Hook<void, BaseEntity<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::construct_hooks.RemoveHook(hook);
	}

	/// @brief Removes a previously added destruct hook for the specified component type.
	///
	/// @tparam TComponent The component type the hook was registered to.
	/// @param hook The hook instance to remove.
	template <typename TComponent>
	void RemoveOnDestruct(const Hook<void, BaseEntity<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::destruct_hooks.RemoveHook(hook);
	}

	/// @brief Removes a previously added update hook for the specified component type.
	///
	/// @tparam TComponent The component type the hook was registered to.
	/// @param hook The hook instance to remove.
	template <typename TComponent>
	void RemoveOnUpdate(const Hook<void, BaseEntity<TArchiver>>& hook) {
		auto component{ GetId<TComponent>() };
		auto pool{ GetOrAddPool<TComponent>(component) };
		pool->template Pool<TComponent, TArchiver>::update_hooks.RemoveHook(hook);
	}

	/// @brief Refreshes the state of the manager.
	///       Cleans up entities marked for deletion and makes the manager aware of newly created
	/// entities.
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
			// BaseEntity was marked for refresh.
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

	/// @brief Reserves memory for the specified number of entities.
	/// @param capacity The capacity to reserve.
	void Reserve(std::size_t capacity) {
		entities_.Reserve(capacity);
		refresh_.Reserve(capacity);
		versions_.reserve(capacity);
		ECS_ASSERT(
			entities_.Capacity() == refresh_.Capacity(),
			"BaseEntity and refresh vectors must have the same capacity"
		);
	}

	/// @brief Creates a new entity. Call Refresh() after using this method.
	/// @return The created BaseEntity.
	BaseEntity<TArchiver> CreateEntity();

	/// @brief Copies an entity from one BaseManager to another.
	/// @tparam TComponents The component types to copy.
	/// @param from The entity to copy from.
	/// @param to The entity to copy to.
	template <typename... TComponents>
	void CopyEntity(const BaseEntity<TArchiver>& from, BaseEntity<TArchiver>& to);

	/// @brief Copies an entity and returns the new entity. Call Refresh() after using this method.
	/// @tparam TComponents The component types to copy.
	/// @param from The entity to copy from.
	/// @return The copied entity.
	template <typename... TComponents>
	BaseEntity<TArchiver> CopyEntity(const BaseEntity<TArchiver>& from);

	/// @brief Retrieves all entities that have the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return A collection of entities that have the specified components.
	template <typename... TComponents>
	[[nodiscard]] ConstViewType<LoopCriterion::WithComponents, TComponents...> EntitiesWith() const;

	/// @brief Retrieves all entities that have the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return A collection of entities that have the specified components.
	template <typename... TComponents>
	[[nodiscard]] ViewType<LoopCriterion::WithComponents, TComponents...> EntitiesWith();

	/// @brief Retrieves all entities that do not have the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return A collection of entities that do not have the specified components.
	template <typename... TComponents>
	[[nodiscard]] ConstViewType<LoopCriterion::WithoutComponents, TComponents...> EntitiesWithout(
	) const;

	/// @brief Retrieves all entities that do not have the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return A collection of entities that do not have the specified components.
	template <typename... TComponents>
	[[nodiscard]] ViewType<LoopCriterion::WithoutComponents, TComponents...> EntitiesWithout();

	/// @brief Retrieves all entities in the manager.
	/// @return A collection of all entities in the manager.
	[[nodiscard]] ConstViewType<LoopCriterion::None> Entities() const;

	/// @brief Retrieves all entities in the manager.
	/// @return A collection of all entities in the manager.
	[[nodiscard]] ViewType<LoopCriterion::None> Entities();

	/// @return The number of active entities in the manager.
	[[nodiscard]] std::size_t Size() const {
		return count_;
	}

	/// @return True if the manager has no active entities, false otherwise.
	[[nodiscard]] bool IsEmpty() const {
		return Size() == 0;
	}

	/// @return The capacity of the manager's active entity storage.
	[[nodiscard]] std::size_t Capacity() const {
		return versions_.capacity();
	}

	/// @brief Clears all entities and resets the manager state.
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

	/// @brief Resets the manager to its initial state, clearing all entities and pools. Shrinks the
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
	friend struct std::hash<BaseEntity<TArchiver>>;
	template <typename TA>
	friend class BaseEntity;
	template <typename TM, LoopCriterion U, typename... TCs>
	friend class View;
	template <typename TE, typename TA, typename... TCs>
	friend class Pools;
	template <typename TC, typename TA>
	friend class Pool;

	virtual void ClearEntities();

	/// @brief Copies an entity's components to another entity.
	///
	/// This function copies components from one entity to another. If specific components are
	/// provided, only those components are copied. Otherwise, all components of the entity are
	/// copied.
	///
	/// @tparam TComponents The component types to copy.
	/// @param from_id The entity ID from which to copy.
	/// @param from_version The version of the entity to copy from.
	/// @param to_id The entity ID to which to copy.
	/// @param to_version The version of the entity to copy to.
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
			Pools<BaseEntity<TArchiver>, TArchiver, TComponents...> pools{
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

	/// @brief Generates a new entity and assigns it a version.
	///
	/// This function generates a new entity, either by picking an available one from the free
	/// entity list or by incrementing the next available entity counter. It also handles resizing
	/// the manager if the entity capacity is exceeded.
	///
	/// @param entity The generated entity index.
	/// @param version The version assigned to the newly created entity.
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
		// BaseEntity version incremented here.
		version = ++versions_[entity];
	}

	/// @brief Resizes the internal storage for entities and their components.
	///
	/// This function adjusts the capacity of the internal storage arrays (such as `entities_`,
	///`refresh_`, and `versions_`) to accommodate the new size.
	///
	/// @param size The new size to resize the storage to.
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

	/// @brief Clears an entity's components from the pools.
	///
	/// This function removes all components associated with an entity from the component pools.
	///
	/// @param entity The entity whose components are to be cleared.
	void ClearEntity(Id entity) const {
		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Remove(*this, entity);
			}
		}
	}

	/// @brief Checks if an entity is alive based on its version and state.
	///
	/// This function determines whether an entity is alive by checking its version and whether it
	/// is in the process of creation or deletion.
	///
	/// @param entity The entity to check.
	/// @param version The version of the entity to check.
	/// @return True if the entity is alive, otherwise false.
	[[nodiscard]] bool IsAlive(Id entity, Version version) const {
		return version != 0 && entity < versions_.size() && versions_[entity] == version &&
			   entity < entities_.Size() &&
			   // BaseEntity considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (entities_[entity] || refresh_[entity]);
	}

	/// @brief Checks if an entity is activated (i.e., exists and is marked as active).
	///
	/// @param entity The entity to check.
	/// @return True if the entity is activated, otherwise false.
	[[nodiscard]] bool IsActivated(Id entity) const {
		return entity < entities_.Size() && entities_[entity];
	}

	[[nodiscard]] Version GetVersion(Id entity) const {
		ECS_ASSERT(entity < versions_.size(), "BaseEntity does not have a valid version");
		return versions_[entity];
	}

	/// @brief Checks if two entities match in terms of components.
	///
	/// This function checks if two entities share the same components, meaning that they have
	/// identical component states.
	///
	/// @param entity1 The first entity to check.
	/// @param entity2 The second entity to check.
	/// @return True if the entities match, otherwise false.
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

	/// @brief Destroys an entity by marking it for deletion and clearing its components.
	///
	/// @param entity The entity to destroy.
	/// @param version The version of the entity to destroy.
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
			/// Edge case where entity is created and marked for deletion
			/// before a Refresh() has been called.
			/// In this case, destroy and invalidate the entity without
			/// a Refresh() call. This is equivalent to an entity which
			/// never 'officially' existed in the manager.
			ClearEntity(entity);
			refresh_.Set(entity, false);
			++versions_[entity];
			free_entities_.emplace_back(entity);
		}
	}

	/// @brief Retrieves the component pool for a given component type and index.
	///
	/// @tparam TComponent The component type to retrieve the pool for.
	/// @param component The index of the component type to retrieve the pool for.
	/// @return The pool of the specified component type.
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

	/// @brief Retrieves the component pool for a given component type and index (non-const).
	///
	/// @tparam TComponent The component type to retrieve the pool for.
	/// @param component The index of the component type to retrieve the pool for.
	/// @return The pool of the specified component type.
	template <typename TComponent>
	[[nodiscard]] Pool<TComponent, TArchiver>* GetPool(Id component) {
		return const_cast<Pool<TComponent, TArchiver>*>(
			std::as_const(*this).template GetPool<TComponent>(component)
		);
	}

	/// @brief Removes a component from an entity.
	///
	/// @tparam TComponent The component type to remove.
	/// @param entity The entity from which to remove the component.
	/// @param component The index of the component to remove.
	template <typename TComponent>
	void Remove(Id entity, Id component) {
		auto pool{ GetPool<TComponent>(component) };
		if (pool != nullptr) {
			pool->template Pool<TComponent, TArchiver>::Remove(*this, entity);
		}
	}

	/// @brief Retrieves the components of an entity.
	///
	/// This function retrieves the components associated with an entity and returns them for
	/// further use.
	///
	/// @tparam TComponents The component types to retrieve.
	/// @param entity The entity to retrieve components from.
	/// @return The components associated with the entity.
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get(Id entity) const {
		Pools<BaseEntity<TArchiver>, TArchiver, TComponents...> p{
			(GetPool<TComponents>(GetId<TComponents>()))...
		};
		return p.Get(entity);
	}

	/// @brief Retrieves the components of an entity (non-const).
	///
	/// @tparam TComponents The component types to retrieve.
	/// @param entity The entity to retrieve components from.
	/// @return The components associated with the entity.
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get(Id entity) {
		Pools<BaseEntity<TArchiver>, TArchiver, TComponents...> p{
			(GetPool<TComponents>(GetId<TComponents>()))...
		};
		return p.Get(entity);
	}

	/// @brief Checks if an entity has a specific component.
	///
	/// @tparam TComponent The component type to check for.
	/// @param entity The entity to check.
	/// @param component The index of the component to check for.
	/// @return True if the entity has the component, otherwise false.
	template <typename TComponent>
	[[nodiscard]] bool Has(Id entity, Id component) const {
		const auto pool{ GetPool<TComponent>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	/// @brief Invokes the specified components' update hooks.
	/// @tparam TComponent The component types to update.
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
		auto pool{ const_cast<BaseManager&>(*this).GetPool<TComponent>(component) };
		if (pool == nullptr) {
			auto new_pool{ std::make_unique<Pool<TComponent, TArchiver>>() };
			pool = new_pool.get();
			ECS_ASSERT(component < pools_.size(), "Component index out of range");
			pools_[component] = std::move(new_pool);
		}
		ECS_ASSERT(pool != nullptr, "Could not create new component pool correctly");
		return pool;
	}

	/// @brief Adds a new component to an entity.
	///
	/// This function adds a new component to the entity, initializing it with the provided
	/// arguments.
	///
	/// @tparam TComponent The component type to add.
	/// @tparam TArgs The constructor arguments for the component.
	/// @param entity The entity to add the component to.
	/// @param component The index of the component to add.
	/// @param constructor_args The arguments used to construct the new component.
	/// @return A reference to the newly added component.
	template <typename TComponent, typename... TArgs>
	TComponent& Add(Id entity, Id component, TArgs&&... constructor_args) {
		auto pool{ GetOrAddPool<TComponent>(component) };
		return pool->Add(*this, entity, std::forward<TArgs>(constructor_args)...);
	}

	/// @brief Retrieves the ID of a component type.
	///
	/// This function returns a unique ID associated with a given component type.
	///
	/// @tparam TComponent The component type to retrieve the ID for.
	/// @return The ID associated with the component type.
	template <typename TComponent>
	[[nodiscard]] static Id GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static Id id{ ComponentCount()++ };
		return id;
	}

	/// @brief Retrieves the count of components.
	///
	/// This function returns the current count of components in the system.
	///
	/// @return The current component count.
	[[nodiscard]] static Id& ComponentCount() {
		static Id id{ 0 };
		return id;
	}

	/// @brief Id of the next available entity.
	Id next_entity_{ 0 };

	/// @brief The total count of active entities.
	Id count_{ 0 };

	/// @brief Flag indicating if a refresh is required.
	bool refresh_required_{ false };

	/// @brief Dynamic bitset tracking the state of entities (alive or dead).
	DynamicBitset entities_;

	/// @brief Dynamic bitset used to track entities that need refreshing.
	DynamicBitset refresh_;

	/// @brief Version vector for entities.
	std::vector<Version> versions_;

	/// @brief Deque of free entity indices.
	std::deque<Id> free_entities_;

	/// @brief Pools of component data for entities.
	/// mutable because EntitiesWith may expand this with empty component pools while remaining
	/// const.
	mutable std::vector<std::unique_ptr<AbstractPool<TArchiver>>> pools_;
};

/// @brief ECS entity handle.
/// Wraps an entity ID, version, and manager.
/// Provides add/remove/check component operations, plus copy, destroy, and compare.
template <typename TArchiver>
class BaseEntity {
public:
	BaseEntity() = default;

	BaseEntity(const BaseEntity&) = default;

	BaseEntity& operator=(const BaseEntity&) = default;

	BaseEntity(BaseEntity&& other) noexcept :
		entity_{ std::exchange(other.entity_, 0) },
		version_{ std::exchange(other.version_, 0) },
		manager_{ std::exchange(other.manager_, nullptr) } {}

	BaseEntity& operator=(BaseEntity&& other) noexcept {
		if (this != &other) {
			entity_	 = std::exchange(other.entity_, 0);
			version_ = std::exchange(other.version_, 0);
			manager_ = std::exchange(other.manager_, nullptr);
		}
		return *this;
	}

	~BaseEntity() noexcept = default;

	/// @return True if the entity is valid, false otherwise.
	explicit operator bool() const {
		return IsAlive();
	}

	/// @return True if the entity ids, versions, and managers are equal. Does not compare
	/// components.
	friend bool operator==(const BaseEntity& a, const BaseEntity& b) {
		return a.entity_ == b.entity_ && a.version_ == b.version_ && a.manager_ == b.manager_;
	}

	/// @return True if there is a mismatch of ids, versions or managers. Does not compare
	/// components.
	friend bool operator!=(const BaseEntity& a, const BaseEntity& b) {
		return !(a == b);
	}

	/// @brief Copies the current entity.
	/// If the entity is invalid, an invalid entity is returned. Refresh the manager for the entity
	/// to appear while looping through manager entities.
	/// @tparam TComponents The component types to copy.
	/// @return A new entity that is a copy of the current one.
	template <typename... TComponents>
	BaseEntity Copy() {
		if (manager_ == nullptr) {
			return {};
		}
		return manager_->template CopyEntity<TComponents...>(*this);
	}

	/// @brief Adds a component to the entity if it does not already have it, otherwise does
	/// nothing.
	/// @tparam TComponent The component type to add.
	/// @tparam TComponents The constructor arguments for the component.
	/// @param constructor_args The arguments to construct the component.
	/// @return A reference to the added or already existing component.
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

	/// @brief Adds a component to the entity. If the entity already has the component, it is
	/// replaced.
	/// @tparam TComponent The component type to add.
	/// @tparam TComponents The constructor arguments for the component.
	/// @param constructor_args The arguments to construct the component.
	/// @return A reference to the added component.
	template <typename TComponent, typename... TComponents>
	TComponent& Add(TComponents&&... constructor_args) {
		ECS_ASSERT(manager_ != nullptr, "Cannot add component to a null entity");
		return manager_->template Add<TComponent>(
			entity_, manager_->template GetId<TComponent>(),
			std::forward<TComponents>(constructor_args)...
		);
	}

	/// @brief Removes components from the entity. If the entity does not have the component, does
	/// nothing.
	/// @tparam TComponents The component types to remove.
	template <typename... TComponents>
	void Remove() {
		if (manager_ == nullptr) {
			return;
		}
		(manager_->template Remove<TComponents>(entity_, manager_->template GetId<TComponents>()),
		 ...);
	}

	/// @brief Checks if the entity has all the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return True if the entity has all specified components, false otherwise.
	template <typename... TComponents>
	[[nodiscard]] bool Has() const {
		return manager_ != nullptr && (manager_->template Has<TComponents>(
										   entity_, manager_->template GetId<TComponents>()
									   ) &&
									   ...);
	}

	/// @brief Checks if the entity has any of the specified components.
	/// @tparam TComponents The component types to check for.
	/// @return True if the entity has any of the specified components, false otherwise.
	template <typename... TComponents>
	[[nodiscard]] bool HasAny() const {
		return manager_ != nullptr && (manager_->template Has<TComponents>(
										   entity_, manager_->template GetId<TComponents>()
									   ) ||
									   ...);
	}

	/// @brief Retrieves the components of the entity.
	/// @tparam TComponents The component types to retrieve.
	/// @return The components of the entity.
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		return manager_->template Get<TComponents...>(entity_);
	}

	/// @brief Retrieves the components of the entity.
	/// @tparam TComponents The component types to retrieve.
	/// @return The components of the entity.
	template <typename... TComponents>
	[[nodiscard]] decltype(auto) Get() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		return manager_->template Get<TComponents...>(entity_);
	}

	/// @brief Retrieve a const pointer to the specified entity component. If entity does not have
	/// the component, returns nullptr.
	/// @tparam TComponent The component type to retrieve.
	/// @return A const pointer component of the entity, or nullptr if the entity has no such
	/// component.
	template <typename TComponent>
	[[nodiscard]] const TComponent* TryGet() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of a null entity");
		if (auto component{ manager_->template GetId<TComponent>() };
			manager_->template Has<TComponent>(entity_, component)) {
			return &manager_->template Get<TComponent>(entity_);
		}
		return nullptr;
	}

	/// @brief Retrieve a pointer to the specified entity component. If entity does not have the
	/// component, returns nullptr.
	/// @tparam TComponent The component type to retrieve.
	/// @return A pointer component of the entity, or nullptr if the entity has no such component.
	template <typename TComponent>
	[[nodiscard]] TComponent* TryGet() {
		return const_cast<TComponent*>(std::as_const(*this).template TryGet<TComponent>());
	}

	/// @brief Invokes the specified components' update hooks.
	/// @tparam TComponents The component types to update.
	template <typename... TComponents>
	void Update() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot update the component of a null entity");
		(manager_->template Update<TComponents>(entity_, manager_->template GetId<TComponents>()),
		 ...);
	}

	/// @brief Clears the entity's components.
	void Clear() const {
		if (manager_ == nullptr) {
			return;
		}
		manager_->ClearEntity(entity_);
	}

	/// @brief Checks if the entity is alive, i.e., if the manager has been refreshed after its
	/// creation.
	/// @return True if the entity is alive, false otherwise.
	[[nodiscard]] bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	/// @brief Destroys the entity, flagging it to be removed from the manager after the next
	/// BaseManager::Refresh() call.
	/// @return *this. Allows for destroying an entity and invalidating its handle in one line:
	/// handle.Destroy() = {};
	BaseEntity& Destroy() {
		if (manager_ != nullptr && manager_->IsAlive(entity_, version_)) {
			manager_->DestroyEntity(entity_, version_);
		}
		return *this;
	}

	/// @brief Gets the manager associated with the entity.
	/// @return A reference to the entity's manager.
	[[nodiscard]] BaseManager<TArchiver>& GetManager() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of a null entity");
		return *manager_;
	}

	/// @brief Gets the manager associated with the entity.
	/// @return A const reference to the entity's manager.
	[[nodiscard]] const BaseManager<TArchiver>& GetManager() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of a null entity");
		return *manager_;
	}

	/// @brief Compares the components of two entities to determine if they are identical.
	/// @param e The entity to compare to.
	/// @return True if the entities are identical, false otherwise.
	[[nodiscard]] bool IsIdenticalTo(const BaseEntity& e) const {
		if (*this == e) {
			return true;
		}

		return entity_ != e.entity_ && manager_ == e.manager_ && manager_ != nullptr
				 ? manager_->Match(entity_, e.entity_)
				 : true;
	}

	/// @brief Retrieves the internal ID/index of the entity.
	///
	/// This ID uniquely identifies the entity within the ECS system's storage.
	/// It is typically used internally for lookups or comparisons.
	/// Note: This should not be used for serialization.
	///
	/// @return The entity's index (ID).
	[[nodiscard]] Id GetId() const {
		return entity_;
	}

	/// @brief Retrieves the version of the entity.
	///
	/// The version distinguishes between different incarnations of an entity
	/// that may reuse the same ID after destruction. Useful for validating handles.
	///
	/// @return The current version of the entity.
	[[nodiscard]] Id GetVersion() const {
		return version_;
	}

protected:
	template <typename TA>
	friend class BaseManager;
	friend struct std::hash<BaseEntity>;
	template <typename TM, LoopCriterion U, typename... TCs>
	friend class View;
	template <typename TE, typename TA, typename... TCs>
	friend class Pools;
	template <typename TC, typename TA>
	friend class Pool;

	/// @brief Constructs an entity with a given ID, version, and associated manager.
	/// @param entity The entity's ID.
	/// @param version The entity's version.
	/// @param manager The manager that owns the entity.
	BaseEntity(Id entity, Version version, const BaseManager<TArchiver>* manager) :
		entity_{ entity },
		version_{ version },
		manager_{ const_cast<BaseManager<TArchiver>*>(manager) } {}

	/// @brief Constructs an entity with a given ID, version, and associated manager.
	/// @param entity The entity's ID.
	/// @param version The entity's version.
	/// @param manager The manager that owns the entity.
	BaseEntity(Id entity, Version version, BaseManager<TArchiver>* manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	/// @brief The entity's ID.
	Id entity_{ 0 };

	/// @brief The entity's version.
	Version version_{ 0 };

	/// @brief The manager that owns the entity.
	BaseManager<TArchiver>* manager_{ nullptr };
};

template <typename TManager, LoopCriterion Criterion, typename... TComponents>
class View {
public:
	using ArchiverType = typename TManager::ArchiverType;

	using EntityType = typename TManager::EntityType;

	using PoolsType = Pools<EntityType, ArchiverType, TComponents...>;

	View() = default;

	View(TManager* manager, Id max_entity, PoolsType pools) :
		manager_{ manager }, max_entity_{ max_entity }, pools_{ std::move(pools) } {}

	template <typename TView>
	class Iterator {
	public:
		using iterator_category = std::forward_iterator_tag;
		using difference_type	= std::ptrdiff_t;
		using pointer			= Id;

		Iterator() = default;

		Iterator(Id entity, TView* view) : entity_{ entity }, view_{ view } {
			SkipInvalid();
			if (!view_->IsMaxEntity(entity_)) {
				ECS_ASSERT(view_->EntityWithinLimit(entity_), "Out-of-range entity index");
			}
		}

		friend bool operator==(const Iterator& a, const Iterator& b) {
			return a.entity_ == b.entity_ && a.view_ == b.view_;
		}

		friend bool operator!=(const Iterator& a, const Iterator& b) {
			return !(a == b);
		}

		Iterator& operator++() {
			++entity_;
			SkipInvalid();
			return *this;
		}

		Iterator operator++(int) {
			auto tmp = *this;
			++(*this);
			return tmp;
		}

		decltype(auto) operator*() const {
			return view_->GetComponentTuple(entity_);
		}

		pointer operator->() const {
			ValidateDeref();
			return entity_;
		}

		pointer GetEntityId() const {
			ValidateDeref();
			return entity_;
		}

	private:
		void SkipInvalid() {
			while (view_->EntityWithinLimit(entity_) && !view_->EntityMeetsCriteria(entity_)) {
				++entity_;
			}
		}

		void ValidateDeref() const {
			ECS_ASSERT(
				view_->ContainsEntity(entity_), "No entity with given components in the view"
			);
		}

		Id entity_{ 0 };
		TView* view_{ nullptr };
	};

	using iterator		 = Iterator<View>;
	using const_iterator = Iterator<const View>;

	iterator begin() {
		return iterator{ 0, this };
	}

	iterator end() {
		return iterator{ max_entity_, this };
	}

	const_iterator begin() const {
		return const_iterator{ 0, this };
	}

	const_iterator end() const {
		return const_iterator{ max_entity_, this };
	}

	const_iterator cbegin() const {
		return const_iterator{ 0, this };
	}

	const_iterator cend() const {
		return const_iterator{ max_entity_, this };
	}

	[[nodiscard]] bool IsEmpty() const {
		return begin() == end();
	}

	/// @return The front entity of the view. Null entity if the view is empty.
	[[nodiscard]] EntityType Front() const {
		if (IsEmpty()) {
			return {};
		}
		return GetEntity(begin().GetEntityId());
	}

	[[nodiscard]] EntityType Back() const {
		if (IsEmpty()) {
			return {};
		}
		auto it{ end() };
		--it;
		return GetEntity(it.GetEntityId());
	}

	template <typename F>
	void operator()(F&& func) {
		for (auto it = begin(); it != end(); ++it) {
			std::apply(std::forward<F>(func), *it);
		}
	}

	template <typename F>
	void operator()(F&& func) const {
		for (auto it = begin(); it != end(); ++it) {
			std::apply(std::forward<F>(func), *it);
		}
	}

	/// @return True if any entity in the view satisfies the predicate.
	template <typename F>
	[[nodiscard]] bool AnyOf(F&& pred) const {
		for (auto it{ begin() }; it != end(); ++it) {
			if (tt::InvokePredicate(std::forward<F>(pred), GetComponentTuple(it.GetEntityId()))) {
				return true;
			}
		}
		return false;
	}

	/// @return True if all entities in the view satisfy the predicate, false if any entity does
	/// not.
	template <typename F>
	[[nodiscard]] bool AllOf(F&& pred) const {
		for (auto it{ begin() }; it != end(); ++it) {
			if (!tt::InvokePredicate(std::forward<F>(pred), GetComponentTuple(it.GetEntityId()))) {
				return false;
			}
		}
		return true;
	}

	/// @return The count of entities in the view that satisfy the predicate.
	template <typename F>
	[[nodiscard]] std::size_t CountIf(F&& pred) const {
		std::size_t count{ 0 };
		for (auto it{ begin() }; it != end(); ++it) {
			if (tt::InvokePredicate(std::forward<F>(pred), GetComponentTuple(it.GetEntityId()))) {
				++count;
			}
		}
		return count;
	}

	/// @return Null entity if no entity satisfies the predicate.
	template <typename F>
	[[nodiscard]] EntityType FindIf(F&& pred) const {
		for (auto it{ begin() }; it != end(); ++it) {
			auto value{ GetComponentTuple(it.GetEntityId()) };
			if (tt::InvokePredicate(std::forward<F>(pred), value)) {
				if constexpr (tt::is_tuple_v<decltype(value)>) {
					return std::get<0>(value);
				} else {
					return value;
				}
			}
		}
		return {};
	}

	/// @brief Invokes the given function for each entity in the view.
	template <typename F>
	void ForEach(F&& func) const {
		for (auto it = begin(); it != end(); ++it) {
			tt::InvokePredicate(std::forward<F>(func), GetComponentTuple(it.GetEntityId()));
		}
	}

	/// @return A vector built by projecting each matching entity.
	template <typename F>
	[[nodiscard]] auto Transform(F&& func) const {
		using Result = std::decay_t<decltype(tt::InvokePredicate(
			std::forward<F>(func), GetComponentTuple(std::declval<Id>())
		))>;

		std::vector<Result> v;
		for (auto it{ begin() }; it != end(); ++it) {
			v.emplace_back(
				tt::InvokePredicate(std::forward<F>(func), GetComponentTuple(it.GetEntityId()))
			);
		}
		return v;
	}

	/// @return A vector of the entities in the view.
	[[nodiscard]] std::vector<EntityType> GetVector() const {
		std::vector<EntityType> v;
		v.reserve(max_entity_);
		for (auto it = begin(); it != end(); ++it) {
			v.emplace_back(GetEntity(it.GetEntityId()));
		}
		v.shrink_to_fit();
		return v;
	}

	/// @return The count of entities in the view.
	[[nodiscard]] std::size_t Count() const {
		std::size_t count{ 0 };
		for (auto it = begin(); it != end(); ++it) {
			count++;
		}
		return count;
	}

	/// @return True if the view contains the given entity, false otherwise.
	[[nodiscard]] bool Contains(const EntityType& entity) const {
		return ContainsEntity(entity.GetId()) &&
			   manager_->GetVersion(entity.GetId()) == entity.GetVersion();
	}

private:
	friend class BaseManager<ArchiverType>;

	[[nodiscard]] bool ContainsEntity(Id entity) const {
		return EntityWithinLimit(entity) && !IsMaxEntity(entity) && EntityMeetsCriteria(entity);
	}

	EntityType GetEntity(Id entity) const {
		ECS_ASSERT(ContainsEntity(entity), "No entity with given components in the view");
		return EntityType{ entity, manager_->GetVersion(entity), manager_ };
	}

	[[nodiscard]] bool EntityMeetsCriteria(Id entity) const {
		if (!manager_->IsActivated(entity)) {
			return false;
		}

		if constexpr (Criterion == LoopCriterion::None) {
			return true;
		} else if constexpr (Criterion == LoopCriterion::WithComponents) {
			return pools_.Has(entity);
		} else { // WithoutComponents
			return pools_.NotHas(entity);
		}
	}

	[[nodiscard]] bool IsMaxEntity(Id entity) const {
		return entity == max_entity_;
	}

	[[nodiscard]] bool EntityWithinLimit(Id entity) const {
		return entity < max_entity_;
	}

	[[nodiscard]] decltype(auto) GetComponentTuple(Id entity) {
		ECS_ASSERT(ContainsEntity(entity), "No entity with given components in the view");
		if constexpr (Criterion == LoopCriterion::WithComponents) {
			return pools_.GetWithEntity(entity, manager_);
		} else {
			return EntityType{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	[[nodiscard]] decltype(auto) GetComponentTuple(Id entity) const {
		ECS_ASSERT(ContainsEntity(entity), "No entity with given components in the view");
		if constexpr (Criterion == LoopCriterion::WithComponents) {
			return pools_.GetWithEntity(entity, manager_);
		} else {
			return EntityType{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	TManager* manager_{ nullptr };
	Id max_entity_{ 0 };
	PoolsType pools_;
};

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::InvokeDestructHooks(const BaseManager<TArchiver>& manager) {
	for (auto entity : dense) {
		destruct_hooks.Invoke(BaseEntity{ entity, manager.GetVersion(entity), &manager });
	}
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Copy(
	const BaseManager<TArchiver>& manager, Id from_entity, Id to_entity
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
			update_hooks.Invoke(BaseEntity{ to_entity, manager.GetVersion(to_entity), &manager });
		}
	} else {
		ECS_ASSERT(false, "Cannot copy an entity with a non copy constructible component");
	}
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Clear(const BaseManager<TArchiver>& manager) {
	InvokeDestructHooks(manager);
	components.clear();
	dense.clear();
	sparse.clear();
}

template <typename TComponent, typename TArchiver>
void Pool<TComponent, TArchiver>::Update(const BaseManager<TArchiver>& manager, Id entity) const {
	ECS_ASSERT(Has(entity), "Cannot update a component which the entity does not have");
	update_hooks.Invoke(BaseEntity{ entity, manager.GetVersion(entity), &manager });
}

template <typename TComponent, typename TArchiver>
bool Pool<TComponent, TArchiver>::Remove(const BaseManager<TArchiver>& manager, Id entity) {
	if (!Has(entity)) {
		return false;
	}
	destruct_hooks.Invoke(BaseEntity{ entity, manager.GetVersion(entity), &manager });

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
	const BaseManager<TArchiver>& manager, Id entity, TArgs&&... constructor_args
) {
	static_assert(
		std::is_constructible_v<TComponent, TArgs...> ||
			tt::is_aggregate_initializable_v<TComponent, TArgs...>,
		"Cannot add component which is not constructible from given arguments"
	);
	if (entity < sparse.size()) {
		// BaseEntity has had the component before.
		if (sparse[entity] < dense.size() && dense[sparse[entity]] == entity) {
			// BaseEntity currently has the component.
			// Replace the current component with a new component.
			TComponent& component{ components[sparse[entity]] };
			component.~TComponent();
			// This approach prevents the creation of a temporary component object.
			if constexpr (std::is_aggregate_v<TComponent>) {
				new (&component) TComponent{ std::forward<TArgs>(constructor_args)... };
			} else {
				new (&component) TComponent(std::forward<TArgs>(constructor_args)...);
			}
			update_hooks.Invoke(BaseEntity{ entity, manager.GetVersion(entity), &manager });
			return component;
		}
		// BaseEntity currently does not have the component.
		sparse[entity] = static_cast<Id>(dense.size());
	} else {
		// BaseEntity has never had the component.
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

	construct_hooks.Invoke(BaseEntity{ entity, manager.GetVersion(entity), &manager });
	return *component;
}

template <typename TEntity, typename TArchiver, typename... TComponents>
[[nodiscard]] constexpr decltype(auto) Pools<TEntity, TArchiver, TComponents...>::GetWithEntity(
	Id entity, const BaseManager<TArchiver>* manager
) const {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(TComponents) > 0);
	return std::tuple<TEntity, const TComponents&...>{
		TEntity{ entity, manager->GetVersion(entity), manager }, GetOne<TComponents>(entity)...
	};
}

template <typename TEntity, typename TArchiver, typename... TComponents>
[[nodiscard]] constexpr decltype(auto) Pools<TEntity, TArchiver, TComponents...>::GetWithEntity(
	Id entity, BaseManager<TArchiver>* manager
) {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(TComponents) > 0);
	return std::tuple<TEntity, TComponents&...>{
		TEntity{ entity, manager->GetVersion(entity), manager }, GetOne<TComponents>(entity)...
	};
}

template <typename TArchiver>
inline BaseEntity<TArchiver> BaseManager<TArchiver>::CreateEntity() {
	Id entity{ 0 };
	Version version{ 0 };
	GenerateEntity(entity, version);
	ECS_ASSERT(version != 0, "Failed to create new entity in manager");
	return BaseEntity{ entity, version, this };
}

template <typename TArchiver>
template <typename... TComponents>
inline void BaseManager<TArchiver>::CopyEntity(
	const BaseEntity<TArchiver>& from, BaseEntity<TArchiver>& to
) {
	CopyEntity<TComponents...>(from.entity_, from.version_, to.entity_, to.version_);
}

template <typename TArchiver>
template <typename... TComponents>
inline BaseEntity<TArchiver> BaseManager<TArchiver>::CopyEntity(const BaseEntity<TArchiver>& from) {
	BaseEntity<TArchiver> to{ CreateEntity() };
	CopyEntity<TComponents...>(from, to);
	return to;
}

template <typename TArchiver>
template <typename... TComponents>
inline BaseManager<TArchiver>::ConstViewType<LoopCriterion::WithComponents, TComponents...>
BaseManager<TArchiver>::EntitiesWith() const {
	return { this, next_entity_,
			 Pools<BaseEntity<TArchiver>, TArchiver, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline BaseManager<TArchiver>::ViewType<LoopCriterion::WithComponents, TComponents...>
BaseManager<TArchiver>::EntitiesWith() {
	return { this, next_entity_,
			 Pools<BaseEntity<TArchiver>, TArchiver, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline BaseManager<TArchiver>::ConstViewType<LoopCriterion::WithoutComponents, TComponents...>
BaseManager<TArchiver>::EntitiesWithout() const {
	return { this, next_entity_,
			 Pools<BaseEntity<TArchiver>, TArchiver, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
template <typename... TComponents>
inline BaseManager<TArchiver>::ViewType<LoopCriterion::WithoutComponents, TComponents...>
BaseManager<TArchiver>::EntitiesWithout() {
	return { this, next_entity_,
			 Pools<BaseEntity<TArchiver>, TArchiver, TComponents...>{
				 GetOrAddPool<TComponents>(GetId<TComponents>())... } };
}

template <typename TArchiver>
inline BaseManager<TArchiver>::ConstViewType<LoopCriterion::None> BaseManager<TArchiver>::Entities(
) const {
	return { this, next_entity_, Pools<BaseEntity<TArchiver>, TArchiver>{} };
}

template <typename TArchiver>
inline BaseManager<TArchiver>::ViewType<LoopCriterion::None> BaseManager<TArchiver>::Entities() {
	return { this, next_entity_, Pools<BaseEntity<TArchiver>, TArchiver>{} };
}

template <typename TArchiver>
inline void BaseManager<TArchiver>::ClearEntities() {
	for (auto entity : Entities()) {
		entity.Destroy();
	}
}

} // namespace impl

using Entity  = ::ecs::impl::BaseEntity<impl::VoidArchiver>;
using Manager = ::ecs::impl::BaseManager<impl::VoidArchiver>;

template <::ecs::impl::LoopCriterion Criterion, typename... TComponents>
using View = ::ecs::impl::View<Manager, Criterion, TComponents...>;

template <::ecs::impl::LoopCriterion Criterion, typename... TComponents>
using ConstView = ::ecs::impl::View<const Manager, Criterion, TComponents...>;

using EntityView	  = Manager::ViewType<impl::LoopCriterion::None>;
using ConstEntityView = Manager::ConstViewType<impl::LoopCriterion::None>;

template <typename... TComponents>
using EntityViewWith = Manager::ViewType<impl::LoopCriterion::WithComponents, TComponents...>;
template <typename... TComponents>
using ConstEntityViewWith =
	Manager::ConstViewType<impl::LoopCriterion::WithComponents, TComponents...>;

template <typename... TComponents>
using EntityViewWithout = Manager::ViewType<impl::LoopCriterion::WithoutComponents, TComponents...>;
template <typename... TComponents>
using ConstEntityViewWithout =
	Manager::ConstViewType<impl::LoopCriterion::WithoutComponents, TComponents...>;

} // namespace ecs

namespace std {

template <typename TArchiver>
struct hash<ecs::impl::BaseEntity<TArchiver>> {
	size_t operator()(const ecs::impl::BaseEntity<TArchiver>& e) const {
		// Source: https://stackoverflow.com/a/17017281
		size_t h{ 17 };
		h = h * 31 + hash<ecs::impl::BaseManager<TArchiver>*>()(e.manager_);
		h = h * 31 + hash<ecs::impl::Id>()(e.entity_);
		h = h * 31 + hash<ecs::impl::Version>()(e.version_);
		return h;
	}
};

} // namespace std