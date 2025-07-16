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
			std::cout << "ASSERTION FAILED: "                                                   \
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

template <typename Archiver>
class Entity;
template <typename Archiver>
class Manager;

namespace impl {

using Index	  = std::uint32_t;
using Version = std::uint32_t;

/**
 * @brief Enum for defining loop criteria for iterating through entities.
 */
enum class LoopCriterion {
	None,
	WithComponents,
	WithoutComponents
};

} // namespace impl

template <
	typename T, typename Archiver, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
class EntityContainer;

template <impl::LoopCriterion Criterion, typename TContainer, typename... Ts>
class EntityContainerIterator;

/**
 * @brief Alias for an entity container with no components for const and non-const entities.
 *
 * @tparam is_const A boolean indicating whether the container is for const entities.
 */
template <typename Archiver, bool is_const>
using Entities = EntityContainer<Entity<Archiver>, Archiver, is_const, impl::LoopCriterion::None>;

/**
 * @brief Alias for an entity container with specific components for const and non-const entities.
 *
 * @tparam is_const A boolean indicating whether the container is for const entities.
 * @tparam TComponents The component types that entities must have.
 */
template <typename Archiver, bool is_const, typename... TComponents>
using EntitiesWith = EntityContainer<
	Entity<Archiver>, Archiver, is_const, impl::LoopCriterion::WithComponents, TComponents...>;

/**
 * @brief Alias for an entity container lacking specific components for const and non-const
 * entities.
 *
 * @tparam is_const A boolean indicating whether the container is for const entities.
 * @tparam TComponents The component types that entities must lack.
 */
template <typename Archiver, bool is_const, typename... TComponents>
using EntitiesWithout = EntityContainer<
	Entity<Archiver>, Archiver, is_const, impl::LoopCriterion::WithoutComponents, TComponents...>;

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
	template <typename T>
	void SetComponent(const T& component);

	template <typename T>
	[[nodiscard]] bool HasComponent() const;

	template <typename T>
	[[nodiscard]] T GetComponent() const;

	template <typename T>
	void SetComponents(const std::vector<T>& value);

	template <typename T>
	void SetArrays(const std::vector<Index>& dense_set, const std::vector<Index>& sparse_set);

	template <typename T>
	[[nodiscard]] std::vector<T> GetComponents() const;

	// @return dense_set, sparse_set
	template <typename T>
	[[nodiscard]] std::pair<std::vector<Index>, std::vector<Index>> GetArrays() const;
};

/**
 * @class AbstractPool
 * @brief Abstract base class for managing pools of components.
 *
 * This class defines the interface for managing a pool of components associated with entities.
 * It is intended to be subclassed for concrete component types.
 *
 * @tparam Archiver A class responsible for serializing component data. Defaults to VoidArchiver,
 *                  which performs no serialization and can be used as a placeholder when
 * serialization is not needed.
 */
template <typename Archiver = VoidArchiver>
class AbstractPool {
public:
	/** @brief Virtual destructor to allow proper cleanup of derived classes. */
	virtual ~AbstractPool() = default;

	/**
	 * @brief Checks if the pool's components are cloneable.
	 *
	 * @return True if the components of the pool can be cloned, otherwise false.
	 */
	[[nodiscard]] virtual bool IsCloneable() const = 0;

	/**
	 * @brief Clones the pool and returns a new instance with the same data.
	 *
	 * @return A unique pointer to a new instance of the pool.
	 */
	[[nodiscard]] virtual std::unique_ptr<AbstractPool> Clone() const = 0;

	/**
	 * @brief Copies a component from one entity to another within the pool.
	 *
	 * @param from_entity The source entity from which to copy the component.
	 * @param to_entity The target entity to which the component will be copied.
	 */
	virtual void Copy(Index from_entity, Index to_entity) = 0;

	/**
	 * @brief Clears the pool, removing all components.
	 *
	 * This method removes all components from the pool but does not reset the size of the pool.
	 */
	virtual void Clear() = 0;

	/**
	 * @brief Resets the pool, clearing all components and freeing memory.
	 *
	 * This method clears the pool and shrinks its memory usage to fit the current size.
	 */
	virtual void Reset() = 0;

	/**
	 * @brief Removes a component from the pool for a given entity.
	 *
	 * @param entity The entity from which to remove the component.
	 * @return True if the component was successfully removed, otherwise false.
	 */
	virtual bool Remove(Index entity) = 0;

	/**
	 * @brief Checks if the pool contains a component for the specified entity.
	 *
	 * @param entity The entity to check.
	 * @return True if the entity has a component in the pool, otherwise false.
	 */
	[[nodiscard]] virtual bool Has(Index entity) const = 0;

	/**
	 * @brief Serializes all components in the pool using the provided archiver.
	 *
	 * This function should be implemented by derived classes to serialize their internal component
	 * data into the given archiver. The format and behavior of serialization depends on the
	 * specific Archiver type.
	 *
	 * @param archiver The archiver instance used to write the serialized component data.
	 */
	virtual void Serialize(Archiver& archiver) const = 0;

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
	virtual void Serialize(Archiver& archiver, Index entity) const = 0;

	/**
	 * @brief Deserializes components into the pool using the provided archiver.
	 *
	 * This function should be implemented by derived classes to reconstruct their internal
	 * component data from the given archiver. The format must match that used during serialization.
	 *
	 * @param archiver The archiver instance used to read and load the component data.
	 */
	virtual void Deserialize(const Archiver& archiver) = 0;

	/**
	 * @brief Deserializes component data for a specific entity from the provided archiver.
	 *
	 * This function should be implemented by derived classes to reconstruct the internal
	 * component data for a single entity from the archiver. The input format must match
	 * the one used during serialization of that entity's component.
	 *
	 * @param archiver The archiver instance used to read and load the component data.
	 * @param entity The index of the entity for which the component data should be deserialized.
	 */
	virtual void Deserialize(const Archiver& archiver, Index entity) = 0;
};

/**
 * @class Pool
 * @brief A template class representing a pool of components of type T with optional serialization
 * support.
 *
 * This class manages a collection of components of type T, providing efficient storage,
 * access, and manipulation of components associated with entities. It inherits from
 * AbstractPool and supports optional serialization through a customizable Archiver.
 *
 * @tparam T         The type of component stored in the pool.
 * @tparam Archiver  The archiver used for serialization and deserialization of components.
 *                   If set to VoidArchiver, serialization is effectively disabled.
 */
template <typename T, typename Archiver>
class Pool : public AbstractPool<Archiver> {
	static_assert(
		std::is_move_constructible_v<T>,
		"Cannot create pool for component which is not move constructible"
	);
	static_assert(
		std::is_destructible_v<T>, "Cannot create pool for component which is not destructible"
	);

public:
	/** @brief Default constructor for the Pool class. */
	Pool() = default;

	/** @brief Move constructor for the Pool class. */
	Pool(Pool&&) noexcept = default;

	/** @brief Move assignment operator for the Pool class. */
	Pool& operator=(Pool&&) noexcept = default;

	/** @brief Copy constructor for the Pool class. */
	Pool(const Pool&) = default;

	/** @brief Copy assignment operator for the Pool class. */
	Pool& operator=(const Pool&) = default;

	/** @brief Destructor for the Pool class. */
	~Pool() override = default;

	/**
	 * @brief Serializes the component data in the pool using the provided archiver.
	 *
	 * @param archiver The archiver instance used to write component data.
	 */
	void Serialize(Archiver& archiver) const override {
		if constexpr (std::is_same_v<Archiver, VoidArchiver>) {
			return;
		} else {
			archiver.template SetComponents<T>(components_);
			archiver.template SetArrays<T>(dense_, sparse_);
		}
	}

	/**
	 * @brief Serializes the component data of a single entity using the provided archiver.
	 *
	 * @param archiver The archiver instance used to write component data.
	 * @param entity The index of the entity whose component should be serialized.
	 */
	void Serialize(Archiver& archiver, Index entity) const override {
		if constexpr (std::is_same_v<Archiver, VoidArchiver>) {
			return;
		} else if (Has(entity)) {
			archiver.template SetComponent<T>(Get(entity));
		}
	}

	/**
	 * @brief Deserializes component data into the pool using the provided archiver.
	 *
	 * @param archiver The archiver instance used to read component data.
	 */
	void Deserialize(const Archiver& archiver) override {
		if constexpr (std::is_same_v<Archiver, VoidArchiver>) {
			return;
		} else {
			components_			 = archiver.template GetComponents<T>();
			auto [dense, sparse] = archiver.template GetArrays<T>();
			dense_				 = dense;
			sparse_				 = sparse;
		}
	}

	/**
	 * @brief Deserializes component data for a single entity using the provided archiver.
	 *
	 * @param archiver The archiver instance used to read component data.
	 * @param entity The index of the entity for which the component data should be deserialized.
	 */
	void Deserialize(const Archiver& archiver, Index entity) override {
		if constexpr (std::is_same_v<Archiver, VoidArchiver>) {
			return;
		} else if (archiver.template HasComponent<T>()) {
			if (Has(entity)) {
				Get(entity) = archiver.template GetComponent<T>();
			} else {
				Add(entity, archiver.template GetComponent<T>());
			}
		}
	}

	/**
	 * @brief Checks if the pool's components are cloneable.
	 *
	 * @return True if the components are copy constructible, otherwise false.
	 */
	[[nodiscard]] bool IsCloneable() const final {
		return std::is_copy_constructible_v<T>;
	}

	/**
	 * @brief Clones the pool and returns a new instance with the same data.
	 *
	 * @return A unique pointer to a new pool instance with the same components.
	 */
	[[nodiscard]] std::unique_ptr<AbstractPool<Archiver>> Clone() const final {
		// The reason this is not statically asserted is because it would disallow move-only
		// component pools.
		if constexpr (std::is_copy_constructible_v<T>) {
			auto pool{ std::make_unique<Pool<T, Archiver>>() };
			pool->components_ = components_;
			pool->dense_	  = dense_;
			pool->sparse_	  = sparse_;
			return pool;
		} else {
			ECS_ASSERT(
				false, "Cannot clone component pool with a non copy constructible component"
			);
			return nullptr;
		}
	}

	/**
	 * @brief Copies a component from one entity to another.
	 *
	 * @param from_entity The source entity from which to copy the component.
	 * @param to_entity The target entity to which the component will be copied.
	 */
	void Copy(Index from_entity, Index to_entity) final {
		// Same reason as given in Clone() for why no static_assert.
		if constexpr (std::is_copy_constructible_v<T>) {
			ECS_ASSERT(
				Has(from_entity), "Cannot copy from an entity which does not exist in the manager"
			);
			if (!Has(to_entity)) {
				Add(to_entity, components_[sparse_[from_entity]]);
			} else {
				components_.emplace(
					components_.begin() + sparse_[to_entity], components_[sparse_[from_entity]]
				);
			}
		} else {
			ECS_ASSERT(false, "Cannot copy an entity with a non copy constructible component");
		}
	}

	/**
	 * @brief Clears the pool, removing all components.
	 *
	 * This method removes all components from the pool.
	 */
	void Clear() final {
		components_.clear();
		dense_.clear();
		sparse_.clear();
	}

	/**
	 * @brief Resets the pool by clearing all components and shrinking memory usage.
	 */
	void Reset() final {
		Clear();

		components_.shrink_to_fit();
		dense_.shrink_to_fit();
		sparse_.shrink_to_fit();
	}

	/**
	 * @brief Removes a component from the pool for a given entity.
	 *
	 * @param entity The entity from which to remove the component.
	 * @return True if the component was successfully removed, otherwise false.
	 */
	bool Remove(Index entity) final {
		if (!Has(entity)) {
			return false;
		}
		// See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for
		// in-depth explanation. In short, swap with back and pop back,
		// relinking sparse ids after.
		auto last{ dense_.back() };
		std::swap(dense_.back(), dense_[sparse_[entity]]);
		std::swap(components_.back(), components_[sparse_[entity]]);
		ECS_ASSERT(last < sparse_.size(), "");
		std::swap(sparse_[last], sparse_[entity]);
		dense_.pop_back();
		components_.pop_back();
		return true;
	}

	/**
	 * @brief Checks if the pool contains a component for the specified entity.
	 *
	 * @param entity The entity to check.
	 * @return True if the entity has a component in the pool, otherwise false.
	 */
	[[nodiscard]] bool Has(Index entity) const final {
		if (entity >= sparse_.size()) {
			return false;
		}
		auto s{ sparse_[entity] };
		if (s >= dense_.size()) {
			return false;
		}
		return entity == dense_[s];
	}

	/**
	 * @brief Retrieves a constant reference to the component associated with the specified entity.
	 *
	 * @param entity The entity whose component is to be retrieved.
	 * @return A constant reference to the component associated with the entity.
	 */
	[[nodiscard]] const T& Get(Index entity) const {
		ECS_ASSERT(Has(entity), "Entity does not have the requested component");
		ECS_ASSERT(
			sparse_[entity] < components_.size(),
			"Likely attempting to retrieve a component before it has been fully added"
		);
		return components_[sparse_[entity]];
	}

	/**
	 * @brief Retrieves a reference to the component associated with the specified entity.
	 *
	 * @param entity The entity whose component is to be retrieved.
	 * @return A reference to the component associated with the entity.
	 */
	[[nodiscard]] T& Get(Index entity) {
		return const_cast<T&>(std::as_const(*this).Get(entity));
	}

	/**
	 * @brief Adds a component to the pool for the specified entity.
	 *
	 * @param entity The entity to which the component will be added.
	 * @param constructor_args Arguments to construct the component.
	 * @return A reference to the newly added component.
	 */
	template <typename... Ts>
	T& Add(Index entity, Ts&&... constructor_args) {
		static_assert(
			std::is_constructible_v<T, Ts...> || tt::is_aggregate_initializable_v<T, Ts...>,
			"Cannot add component which is not constructible from given arguments"
		);
		if (entity < sparse_.size()) {
			// Entity has had the component before.
			if (sparse_[entity] < dense_.size() && dense_[sparse_[entity]] == entity) {
				// Entity currently has the component.
				// Replace the current component with a new component.
				T& component{ components_[sparse_[entity]] };
				component.~T();
				// This approach prevents the creation of a temporary component object.
				if constexpr (std::is_aggregate_v<T>) {
					new (&component) T{ std::forward<Ts>(constructor_args)... };
				} else {
					new (&component) T(std::forward<Ts>(constructor_args)...);
				}
				return component;
			}
			// Entity currently does not have the component.
			sparse_[entity] = static_cast<Index>(dense_.size());
		} else {
			// Entity has never had the component.
			sparse_.resize(static_cast<std::size_t>(entity) + 1, static_cast<Index>(dense_.size()));
		}
		// Add new component to the entity.
		dense_.push_back(entity);
		if constexpr (std::is_aggregate_v<T>) {
			return components_.emplace_back(std::move(T{ std::forward<Ts>(constructor_args)... }));
		} else {
			return components_.emplace_back(std::forward<Ts>(constructor_args)...);
		}
	}

private:
	// @brief The vector storing components of type T.
	std::vector<T> components_;

	// @brief The dense vector indexing entities.
	std::vector<Index> dense_;

	// @brief The sparse vector indexing components.
	std::vector<Index> sparse_;
};

/**
 * @class Pools
 * @brief A template class for managing multiple pools of components.
 *
 * This class allows for accessing multiple pools of components (of different types) in a type-safe
 * manner.
 */
template <typename T, typename Archiver, bool is_const, typename... Ts>
class Pools {
public:
	template <typename TPool>
	using PoolType =
		std::conditional_t<is_const, const Pool<TPool, Archiver>*, Pool<TPool, Archiver>*>;

	/**
	 * @brief Constructor that initializes the pools with the provided pool instances.
	 *
	 * @param pools The pools to initialize the manager with.
	 */
	explicit constexpr Pools(PoolType<Ts>... pools) :
		pools_{ std::tuple<PoolType<Ts>...>(pools...) } {}

	/**
	 * @brief Copies components from one entity to another across all pools.
	 *
	 * @param from_id The source entity.
	 * @param to_id The target entity.
	 */
	constexpr void Copy(Index from_id, Index to_id) {
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Copy(from_id, to_id), ...);
	}

	/**
	 * @brief Checks if all requested components exist for a given entity.
	 *
	 * @param entity The entity to check.
	 * @return True if the entity exists in all pools, otherwise false.
	 */
	[[nodiscard]] constexpr bool Has(Index entity) const {
		return AllExist() &&
			   (std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Has(entity) && ...);
	}

	/**
	 * @brief Checks if at least one of the requested components does not exist for the entity.
	 *
	 * @param entity The entity to check.
	 * @return True if the entity is missing at least one of the requested components, otherwise
	 * false.
	 */
	[[nodiscard]] constexpr bool NotHas(Index entity) const {
		return ((std::get<PoolType<Ts>>(pools_) == nullptr) || ...) ||
			   (!std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Has(entity) && ...);
	}

	/**
	 * @brief Retrieves the requested components along with the entity.
	 *
	 * @param entity The entity whose components are to be retrieved.
	 * @param manager The manager for the entity.
	 * @return A tuple of the requested components.
	 */
	[[nodiscard]] constexpr decltype(auto) GetWithEntity(
		Index entity, const Manager<Archiver>* manager
	) const;

	/**
	 * @brief Retrieves the requested components along with the entity.
	 *
	 * @param entity The entity whose components are to be retrieved.
	 * @param manager The manager for the entity.
	 * @return A tuple of the requested components.
	 */
	[[nodiscard]] constexpr decltype(auto) GetWithEntity(Index entity, Manager<Archiver>* manager);

	/**
	 * @brief Retrieves the requested components for a given entity.
	 *
	 * @param entity The entity whose components are to be retrieved.
	 * @return A tuple of references to the requested components.
	 */
	[[nodiscard]] constexpr decltype(auto) Get(Index entity) const {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(Ts) > 0);
		if constexpr (sizeof...(Ts) == 1) {
			return (std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity), ...);
		} else {
			return std::forward_as_tuple<const Ts&...>(
				(std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity))...
			);
		}
	}

	/**
	 * @brief Retrieves the requested components for a given entity.
	 *
	 * @param entity The entity whose components are to be retrieved.
	 * @return A tuple of references to the requested components.
	 */
	[[nodiscard]] constexpr decltype(auto) Get(Index entity) {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(Ts) > 0);
		if constexpr (sizeof...(Ts) == 1) {
			return (std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity), ...);
		} else {
			return std::forward_as_tuple<Ts&...>(
				(std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity))...
			);
		}
	}

	/**
	 * @brief Checks if all the pools are non-null.
	 *
	 * @return True if all pools exist, otherwise false.
	 */
	[[nodiscard]] constexpr bool AllExist() const {
		return ((std::get<PoolType<Ts>>(pools_) != nullptr) && ...);
	}

private:
	// @brief A tuple containing all the pools managed by this class.
	std::tuple<PoolType<Ts>...> pools_;
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

	/**
	 * @brief Compares two DynamicBitset objects for equality.
	 *
	 * This method compares the internal data of two DynamicBitset objects and returns true if they
	 * are identical (same data and size), otherwise returns false.
	 *
	 * @param other The other DynamicBitset object to compare to.
	 * @return True if both bitsets are equal, false otherwise.
	 */
	bool operator==(const DynamicBitset& other) const {
		return data_ == other.data_;
	}

	/**
	 * @brief Returns the number of bits currently tracked by the bitset.
	 *
	 * @return The number of bits in the bitset.
	 */
	[[nodiscard]] std::size_t Size() const {
		return bit_count_;
	}

	/**
	 * @brief Returns the current capacity of the bitset.
	 *
	 * This is the number of bits that can be stored before resizing the internal storage.
	 *
	 * @return The current capacity of the bitset in bits.
	 */
	[[nodiscard]] std::size_t Capacity() const {
		return data_.capacity();
	}

	/**
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

	/**
	 * @brief Retrieves the number of set bits.
	 *
	 * @return The total number of bits in the bitset.
	 */
	[[nodiscard]] std::size_t GetBitCount() const {
		return bit_count_;
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

	// @brief The total number of bits in the bitset.
	std::size_t bit_count_{ 0 };

	// TODO: Move to std::byte instead of std::uint8_t.

	// @brief The internal storage for the bitset (as a vector of bytes).
	std::vector<std::uint8_t> data_;
};

} // namespace impl

/**
 * @class Manager
 * @brief A class responsible for managing entities in the entity component system (ECS).
 *
 * The Manager class oversees the lifecycle of entities, including their creation, deletion,
 * refreshing, and component management within an entity-component system. It provides various
 * utility methods for manipulating entities, such as copying, querying, and clearing entities.
 * It also supports serialization and deserialization of entities and their components through
 * an optional Archiver, allowing the state of the ECS to be saved or loaded.
 *
 * @tparam Archiver The type of archiver used for serializing and deserializing entity and component
 * data. By default, the archiver is set to `impl::VoidArchiver`, which disables serialization.
 */
template <typename Archiver = impl::VoidArchiver>
class Manager {
public:
	/**
	 * @brief Default constructor for the manager.
	 */
	Manager() = default;

	/**
	 * @brief Deleted copy constructor to prevent copying of Manager objects.
	 */
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

	/**
	 * @brief Deleted copy assignment operator to prevent assignment of Manager objects.
	 * @return This object, unchanged.
	 */
	Manager& operator=(const Manager& other) {
		if (this != &other) {
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

	/**
	 * @brief Move constructor for the manager.
	 * @param other The Manager to move from.
	 */
	Manager(Manager&& other) noexcept :
		next_entity_{ std::exchange(other.next_entity_, 0) },
		count_{ std::exchange(other.count_, 0) },
		refresh_required_{ std::exchange(other.refresh_required_, false) },
		entities_{ std::exchange(other.entities_, {}) },
		refresh_{ std::exchange(other.refresh_, {}) },
		versions_{ std::exchange(other.versions_, {}) },
		free_entities_{ std::exchange(other.free_entities_, {}) },
		pools_{ std::exchange(other.pools_, {}) } {}

	/**
	 * @brief Move assignment operator for the manager.
	 * @param other The Manager to move from.
	 * @return A reference to this object.
	 */
	Manager& operator=(Manager&& other) noexcept {
		if (this != &other) {
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

	/**
	 * @brief Destructor for the manager.
	 */
	virtual ~Manager() = default;

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
		impl::Index alive{ 0 };
		impl::Index dead{ 0 };
		for (impl::Index entity{ 0 }; entity < next_entity_; ++entity) {
			if (!refresh_[entity]) {
				continue;
			}
			// Entity was marked for refresh.
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
			"Entity and refresh vectors must have the same capacity"
		);
	}

	/**
	 * @brief Creates a new entity. Call Refresh() after using this method.
	 * @return The created Entity.
	 */
	Entity<Archiver> CreateEntity();

	/**
	 * @brief Copies an entity from one Manager to another.
	 * @tparam Ts The component types to copy.
	 * @param from The entity to copy from.
	 * @param to The entity to copy to.
	 */
	template <typename... Ts>
	void CopyEntity(const Entity<Archiver>& from, Entity<Archiver>& to);

	/**
	 * @brief Copies an entity and returns the new entity. Call Refresh() after using this method.
	 * @tparam Ts The component types to copy.
	 * @param from The entity to copy from.
	 * @return The copied entity.
	 */
	template <typename... Ts>
	Entity<Archiver> CopyEntity(const Entity<Archiver>& from);

	/**
	 * @brief Retrieves all entities that have the specified components.
	 * @tparam Ts The component types to check for.
	 * @return A collection of entities that have the specified components.
	 */
	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWith<Archiver, true, Ts...> EntitiesWith() const;

	/**
	 * @brief Retrieves all entities that have the specified components.
	 * @tparam Ts The component types to check for.
	 * @return A collection of entities that have the specified components.
	 */
	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWith<Archiver, false, Ts...> EntitiesWith();

	/**
	 * @brief Retrieves all entities that do not have the specified components.
	 * @tparam Ts The component types to check for.
	 * @return A collection of entities that do not have the specified components.
	 */
	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWithout<Archiver, true, Ts...> EntitiesWithout() const;

	/**
	 * @brief Retrieves all entities that do not have the specified components.
	 * @tparam Ts The component types to check for.
	 * @return A collection of entities that do not have the specified components.
	 */
	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWithout<Archiver, false, Ts...> EntitiesWithout();

	/**
	 * @brief Retrieves all entities in the manager.
	 * @return A collection of all entities in the manager.
	 */
	[[nodiscard]] ecs::Entities<Archiver, true> Entities() const;

	/**
	 * @brief Retrieves all entities in the manager.
	 * @return A collection of all entities in the manager.
	 */
	[[nodiscard]] ecs::Entities<Archiver, false> Entities();

	/**
	 * @brief Gets the current number of entities in the manager.
	 * @return The number of entities.
	 */
	[[nodiscard]] std::size_t Size() const {
		return count_;
	}

	/**
	 * @brief Checks if the manager has any entities.
	 * @return True if the manager has no entities, false otherwise.
	 */
	[[nodiscard]] bool IsEmpty() const {
		return Size() == 0;
	}

	/**
	 * @brief Gets the capacity of the manager's entity storage.
	 * @return The capacity of the storage.
	 */
	[[nodiscard]] std::size_t Capacity() const {
		return versions_.capacity();
	}

	/**
	 * @brief Clears all entities and resets the manager state.
	 */
	void Clear() {
		ClearEntities();

		count_			  = 0;
		next_entity_	  = 0;
		refresh_required_ = false;

		entities_.Clear();
		refresh_.Clear();
		versions_.clear();
		free_entities_.clear();

		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Clear();
			}
		}
	}

	/**
	 * @brief Resets the manager to its initial state, clearing all entities and pools.
	 *        Shrinks the capacity of the storage.
	 */
	void Reset() {
		Clear();

		entities_.ShrinkToFit();
		refresh_.ShrinkToFit();
		versions_.shrink_to_fit();
		free_entities_.shrink_to_fit();

		pools_.clear();
		pools_.shrink_to_fit();
	}

protected:
	friend struct std::hash<Entity<Archiver>>;
	template <typename A>
	friend class Entity;
	template <typename T, typename A, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
	friend class EntityContainer;
	template <typename T, typename A, bool is_const, typename... Ts>
	friend class impl::Pools;
	template <typename T, typename A>
	friend class impl::Pool;

	virtual void ClearEntities();

	/**
	 * @brief Copies an entity's components to another entity.
	 *
	 * This function copies components from one entity to another. If specific components are
	 * provided, only those components are copied. Otherwise, all components of the entity are
	 * copied.
	 *
	 * @tparam Ts The component types to copy.
	 * @param from_id The entity ID from which to copy.
	 * @param from_version The version of the entity to copy from.
	 * @param to_id The entity ID to which to copy.
	 * @param to_version The version of the entity to copy to.
	 */
	template <typename... Ts>
	void CopyEntity(
		impl::Index from_id, impl::Version from_version, impl::Index to_id, impl::Version to_version
	) {
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

		if constexpr (sizeof...(Ts) > 0) { // Copy only specific components.
			static_assert(
				std::conjunction_v<std::is_copy_constructible<Ts>...>,
				"Cannot copy entity with a component that is not copy constructible"
			);
			impl::Pools<Entity<Archiver>, Archiver, false, Ts...> pools{ GetPool<Ts>(GetId<Ts>()
			)... };
			// Validate if the pools exist and contain the required components
			ECS_ASSERT(
				pools.AllExist(), "Cannot copy entity with a component that is not "
								  "even in the manager"
			);
			ECS_ASSERT(
				pools.Has(from_id), "Cannot copy entity with a component that it does not have"
			);
			pools.Copy(from_id, to_id);
		} else { // Copy all components.
			// Loop through all pools and copy components
			for (auto& pool : pools_) {
				if (pool != nullptr && pool->Has(from_id)) {
					pool->Copy(from_id, to_id);
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
	void GenerateEntity(impl::Index& entity, impl::Version& version) {
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
		// Entity version incremented here.
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
	void ClearEntity(impl::Index entity) const {
		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Remove(entity);
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
	[[nodiscard]] bool IsAlive(impl::Index entity, impl::Version version) const {
		return version != 0 && entity < versions_.size() && versions_[entity] == version &&
			   entity < entities_.Size() &&
			   // Entity considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (entities_[entity] || refresh_[entity]);
	}

	/**
	 * @brief Checks if an entity is activated (i.e., exists and is marked as active).
	 *
	 * @param entity The entity to check.
	 * @return True if the entity is activated, otherwise false.
	 */
	[[nodiscard]] bool IsActivated(impl::Index entity) const {
		return entity < entities_.Size() && entities_[entity];
	}

	/**
	 * @brief Retrieves the version of an entity.
	 *
	 * @param entity The entity whose version is to be retrieved.
	 * @return The version of the entity.
	 */
	[[nodiscard]] impl::Version GetVersion(impl::Index entity) const {
		ECS_ASSERT(entity < versions_.size(), "");
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
	[[nodiscard]] bool Match(impl::Index entity1, impl::Index entity2) const {
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
	void DestroyEntity(impl::Index entity, impl::Version version) {
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
	 * @tparam T The component type to retrieve the pool for.
	 * @param component The index of the component type to retrieve the pool for.
	 * @return The pool of the specified component type.
	 */
	template <typename T>
	[[nodiscard]] const impl::Pool<T, Archiver>* GetPool(impl::Index component) const {
		ECS_ASSERT(component == GetId<T>(), "GetPool mismatch with component id");
		if (component < pools_.size()) {
			const auto& pool{ pools_[component] };
			// This is nullptr if the pool does not exist in the manager.
			return static_cast<impl::Pool<T, Archiver>*>(pool.get());
		}
		return nullptr;
	}

	/**
	 * @brief Retrieves the component pool for a given component type and index (non-const).
	 *
	 * @tparam T The component type to retrieve the pool for.
	 * @param component The index of the component type to retrieve the pool for.
	 * @return The pool of the specified component type.
	 */
	template <typename T>
	[[nodiscard]] impl::Pool<T, Archiver>* GetPool(impl::Index component) {
		return const_cast<impl::Pool<T, Archiver>*>(
			std::as_const(*this).template GetPool<T>(component)
		);
	}

	/**
	 * @brief Removes a component from an entity.
	 *
	 * @tparam T The component type to remove.
	 * @param entity The entity from which to remove the component.
	 * @param component The index of the component to remove.
	 */
	template <typename T>
	void Remove(impl::Index entity, impl::Index component) {
		auto pool{ GetPool<T>(component) };
		if (pool != nullptr) {
			pool->template Pool<T, Archiver>::Remove(entity);
		}
	}

	/**
	 * @brief Retrieves the components of an entity.
	 *
	 * This function retrieves the components associated with an entity and returns them for further
	 * use.
	 *
	 * @tparam Ts The component types to retrieve.
	 * @param entity The entity to retrieve components from.
	 * @return The components associated with the entity.
	 */
	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(impl::Index entity) const {
		impl::Pools<Entity<Archiver>, Archiver, false, Ts...> p{ (GetPool<Ts>(GetId<Ts>()))... };
		return p.Get(entity);
	}

	/**
	 * @brief Retrieves the components of an entity (non-const).
	 *
	 * @tparam Ts The component types to retrieve.
	 * @param entity The entity to retrieve components from.
	 * @return The components associated with the entity.
	 */
	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(impl::Index entity) {
		impl::Pools<Entity<Archiver>, Archiver, false, Ts...> p{ (GetPool<Ts>(GetId<Ts>()))... };
		return p.Get(entity);
	}

	/**
	 * @brief Checks if an entity has a specific component.
	 *
	 * @tparam T The component type to check for.
	 * @param entity The entity to check.
	 * @param component The index of the component to check for.
	 * @return True if the entity has the component, otherwise false.
	 */
	template <typename T>
	[[nodiscard]] bool Has(impl::Index entity, impl::Index component) const {
		const auto pool{ GetPool<T>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	template <typename T>
	impl::Pool<T, Archiver>* GetOrAddPool(impl::Index component) {
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1);
		}
		auto pool{ GetPool<T>(component) };
		if (pool == nullptr) {
			auto new_pool{ std::make_unique<impl::Pool<T, Archiver>>() };
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
	 * @tparam T The component type to add.
	 * @tparam Ts The constructor arguments for the component.
	 * @param entity The entity to add the component to.
	 * @param component The index of the component to add.
	 * @param constructor_args The arguments used to construct the new component.
	 * @return A reference to the newly added component.
	 */
	template <typename T, typename... Ts>
	T& Add(impl::Index entity, impl::Index component, Ts&&... constructor_args) {
		auto pool{ GetOrAddPool<T>(component) };
		return pool->Add(entity, std::forward<Ts>(constructor_args)...);
	}

	/**
	 * @brief Retrieves the ID of a component type.
	 *
	 * This function returns a unique ID associated with a given component type.
	 *
	 * @tparam T The component type to retrieve the ID for.
	 * @return The ID associated with the component type.
	 */
	template <typename T>
	[[nodiscard]] static impl::Index GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static impl::Index id{ ComponentCount()++ };
		return id;
	}

	/**
	 * @brief Retrieves the count of components.
	 *
	 * This function returns the current count of components in the system.
	 *
	 * @return The current component count.
	 */
	[[nodiscard]] static impl::Index& ComponentCount() {
		static impl::Index id{ 0 };
		return id;
	}

	// @brief Index of the next available entity.
	impl::Index next_entity_{ 0 };

	// @brief The total count of active entities.
	impl::Index count_{ 0 };

	// @brief Flag indicating if a refresh is required.
	bool refresh_required_{ false };

	// @brief Dynamic bitset tracking the state of entities (alive or dead).
	impl::DynamicBitset entities_;

	// @brief Dynamic bitset used to track entities that need refreshing.
	impl::DynamicBitset refresh_;

	// @brief Version vector for entities.
	std::vector<impl::Version> versions_;

	// @brief Deque of free entity indices.
	std::deque<impl::Index> free_entities_;

	// @brief Pools of component data for entities.
	std::vector<std::unique_ptr<impl::AbstractPool<Archiver>>> pools_;
};

/**
 * @class Entity
 * @brief A class representing an entity in the ECS (Entity-Component-System) pattern.
 *
 * The Entity class encapsulates an entity's ID, version, and its associated manager.
 * It provides functions for adding, removing, and checking components as well as copying,
 * destroying, and comparing entities.
 */
template <typename Archiver = impl::VoidArchiver>
class Entity {
public:
	/**
	 * @brief Default constructor for the Entity.
	 */
	Entity() = default;

	/**
	 * @brief Copy constructor for the Entity.
	 * @param other The other entity to copy.
	 */
	Entity(const Entity&) = default;

	/**
	 * @brief Copy assignment operator for the Entity.
	 * @param other The other entity to assign.
	 * @return A reference to this entity.
	 */
	Entity& operator=(const Entity&) = default;

	/**
	 * @brief Move constructor for the Entity.
	 * @param other The other entity to move.
	 */
	Entity(Entity&& other) noexcept :
		entity_{ std::exchange(other.entity_, 0) },
		version_{ std::exchange(other.version_, 0) },
		manager_{ std::exchange(other.manager_, nullptr) } {}

	/**
	 * @brief Move assignment operator for the Entity.
	 * @param other The other entity to assign.
	 * @return A reference to this entity.
	 */
	Entity& operator=(Entity&& other) noexcept {
		if (this != &other) {
			entity_	 = std::exchange(other.entity_, 0);
			version_ = std::exchange(other.version_, 0);
			manager_ = std::exchange(other.manager_, nullptr);
		}
		return *this;
	}

	/**
	 * @brief Destructor for the Entity.
	 */
	~Entity() = default;

	/**
	 * @brief Converts the Entity to a boolean, returning true if the entity is valid (exists).
	 * @return True if the entity is valid, false otherwise.
	 */
	explicit operator bool() const;

	/**
	 * @brief Equality operator for comparing two entities.
	 * @param a The first entity to compare.
	 * @param b The second entity to compare.
	 * @return True if the entities are equal, false otherwise.
	 */
	friend bool operator==(const Entity& a, const Entity& b) {
		return a.entity_ == b.entity_ && a.version_ == b.version_ && a.manager_ == b.manager_;
	}

	/**
	 * @brief Inequality operator for comparing two entities.
	 * @param a The first entity to compare.
	 * @param b The second entity to compare.
	 * @return True if the entities are not equal, false otherwise.
	 */
	friend bool operator!=(const Entity& a, const Entity& b) {
		return !(a == b);
	}

	/**
	 * @brief Copies the current entity.
	 * If the entity is invalid or destroyed, a new entity is returned.
	 * @tparam Ts The component types to copy.
	 * @return A new entity that is a copy of the current one.
	 */
	template <typename... Ts>
	Entity Copy() {
		if (manager_ == nullptr) {
			return {};
		}
		return manager_->template CopyEntity<Ts...>(*this);
	}

	/**
	 * @brief Adds a component to the entity.
	 * @tparam T The component type to add.
	 * @tparam Ts The constructor arguments for the component.
	 * @param constructor_args The arguments to construct the component.
	 * @return A reference to the added component.
	 */
	template <typename T, typename... Ts>
	T& Add(Ts&&... constructor_args) {
		ECS_ASSERT(manager_ != nullptr, "Cannot add component to null entity");
		return manager_->template Add<T>(
			entity_, manager_->template GetId<T>(), std::forward<Ts>(constructor_args)...
		);
	}

	/**
	 * @brief Removes components from the entity.
	 * @tparam Ts The component types to remove.
	 */
	template <typename... Ts>
	void Remove() {
		if (manager_ == nullptr) {
			return;
		}
		(manager_->template Remove<Ts>(entity_, manager_->template GetId<Ts>()), ...);
	}

	/**
	 * @brief Checks if the entity has all specified components.
	 * @tparam Ts The component types to check for.
	 * @return True if the entity has all specified components, false otherwise.
	 */
	template <typename... Ts>
	[[nodiscard]] bool Has() const {
		return manager_ != nullptr &&
			   (manager_->template Has<Ts>(entity_, manager_->template GetId<Ts>()) && ...);
	}

	/**
	 * @brief Checks if the entity has any of the specified components.
	 * @tparam Ts The component types to check for.
	 * @return True if the entity has any of the specified components, false otherwise.
	 */
	template <typename... Ts>
	[[nodiscard]] bool HasAny() const {
		return manager_ != nullptr &&
			   (manager_->template Has<Ts>(entity_, manager_->template GetId<Ts>()) || ...);
	}

	/**
	 * @brief Retrieves the components of the entity.
	 * @tparam Ts The component types to retrieve.
	 * @return The components of the entity.
	 */
	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of null entity");
		return manager_->template Get<Ts...>(entity_);
	}

	/**
	 * @brief Retrieves the components of the entity.
	 * @tparam Ts The component types to retrieve.
	 * @return The components of the entity.
	 */
	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of null entity");
		return manager_->template Get<Ts...>(entity_);
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
	Entity& Destroy() {
		if (manager_ != nullptr && manager_->IsAlive(entity_, version_)) {
			manager_->DestroyEntity(entity_, version_);
		}
		return *this;
	}

	/**
	 * @brief Gets the manager associated with the entity.
	 * @return A reference to the entity's manager.
	 */
	[[nodiscard]] Manager<Archiver>& GetManager() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of null entity");
		return *manager_;
	}

	/**
	 * @brief Gets the manager associated with the entity.
	 * @return A const reference to the entity's manager.
	 */
	[[nodiscard]] const Manager<Archiver>& GetManager() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of null entity");
		return *manager_;
	}

	/**
	 * @brief Compares the components of two entities to determine if they are identical.
	 * @param e The entity to compare to.
	 * @return True if the entities are identical, false otherwise.
	 */
	[[nodiscard]] bool IsIdenticalTo(const Entity& e) const {
		if (*this == e) {
			return true;
		}

		return entity_ != e.entity_ && manager_ == e.manager_ && manager_ != nullptr
				 ? manager_->Match(entity_, e.entity_)
				 : true;
	}

protected:
	template <typename A>
	friend class Manager;
	friend struct std::hash<Entity>;
	template <typename T, typename A, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
	friend class EntityContainer;
	template <typename T, typename A, bool is_const, typename... Ts>
	friend class impl::Pools;

	/**
	 * @brief Constructs an entity with a given ID, version, and associated manager.
	 * @param entity The entity's ID.
	 * @param version The entity's version.
	 * @param manager The manager that owns the entity.
	 */
	Entity(impl::Index entity, impl::Version version, const Manager<Archiver>* manager) :
		entity_{ entity },
		version_{ version },
		manager_{ const_cast<Manager<Archiver>*>(manager) } {}

	/**
	 * @brief Constructs an entity with a given ID, version, and associated manager.
	 * @param entity The entity's ID.
	 * @param version The entity's version.
	 * @param manager The manager that owns the entity.
	 */
	Entity(impl::Index entity, impl::Version version, Manager<Archiver>* manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	// @brief The entity's ID.
	impl::Index entity_{ 0 };

	// @brief The entity's version.
	impl::Version version_{ 0 };

	// @brief The manager that owns the entity.
	Manager<Archiver>* manager_{ nullptr };
};

template <typename Archiver>
inline Entity<Archiver>::operator bool() const {
	return *this != Entity<Archiver>{};
}

template <impl::LoopCriterion Criterion, typename TContainer, typename... Ts>
class EntityContainerIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	using difference_type	= std::ptrdiff_t;
	using pointer			= impl::Index;
	// using value_type		= std::tuple<Entity, Ts...> || Entity;
	// using reference			= std::tuple<Entity, Ts&...>|| Entity;

	/**
	 * @brief Default constructor for the iterator.
	 */
	EntityContainerIterator() = default;

	/**
	 * @brief Assignment operator for setting the entity index.
	 * @param entity The entity index to assign to the iterator.
	 * @return A reference to the current iterator.
	 */
	EntityContainerIterator& operator=(pointer entity) {
		entity_ = entity;
		return *this;
	}

	/**
	 * @brief Equality comparison operator for two iterators.
	 * @param a The first iterator.
	 * @param b The second iterator.
	 * @return True if the iterators point to the same entity, false otherwise.
	 */
	friend bool operator==(const EntityContainerIterator& a, const EntityContainerIterator& b) {
		return a.entity_ == b.entity_;
	}

	/**
	 * @brief Inequality comparison operator for two iterators.
	 * @param a The first iterator.
	 * @param b The second iterator.
	 * @return True if the iterators point to different entities, false otherwise.
	 */
	friend bool operator!=(const EntityContainerIterator& a, const EntityContainerIterator& b) {
		return !(a == b);
	}

	/**
	 * @brief Advances the iterator by the specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A reference to the current iterator after moving.
	 */
	EntityContainerIterator& operator+=(const difference_type& movement) {
		entity_ += movement;
		return *this;
	}

	/**
	 * @brief Moves the iterator backwards by the specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A reference to the current iterator after moving.
	 */
	EntityContainerIterator& operator-=(const difference_type& movement) {
		entity_ -= movement;
		return *this;
	}

	/**
	 * @brief Pre-increment operator for the iterator.
	 * @return A reference to the incremented iterator.
	 */
	EntityContainerIterator& operator++() {
		do {
			entity_++;
		} while (ShouldIncrement());
		return *this;
	}

	/**
	 * @brief Post-increment operator for the iterator.
	 * @return A temporary copy of the iterator before incrementing.
	 */
	EntityContainerIterator operator++(int) {
		auto temp(*this);
		++(*this);
		return temp;
	}

	/**
	 * @brief Addition operator to advance the iterator by a specified number of steps.
	 * @param movement The number of steps to move the iterator.
	 * @return A new iterator advanced by the given steps.
	 */
	EntityContainerIterator operator+(const difference_type& movement) {
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
		return entity_container_.GetComponentTuple(entity_);
	}

	/**
	 * @brief Member access operator for retrieving the entity index.
	 * @return The entity index.
	 */
	pointer operator->() const {
		ECS_ASSERT(
			entity_container_.EntityMeetsCriteria(entity_), "No entity with given components"
		);
		ECS_ASSERT(entity_container_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(
			!entity_container_.IsMaxEntity(entity_),
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

	/**
	 * @brief Retrieves the entity index associated with the iterator.
	 * @return The entity index.
	 */
	pointer GetEntityId() const {
		ECS_ASSERT(
			entity_container_.EntityMeetsCriteria(entity_), "No entity with given components"
		);
		ECS_ASSERT(entity_container_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(
			!entity_container_.IsMaxEntity(entity_),
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

private:
	/**
	 * @brief Helper function to determine whether the iterator should be incremented.
	 * @return True if the iterator should be incremented, false otherwise.
	 */
	[[nodiscard]] bool ShouldIncrement() const {
		return entity_container_.EntityWithinLimit(entity_) &&
			   !entity_container_.EntityMeetsCriteria(entity_);
	}

	/**
	 * @brief Constructor that initializes the iterator with the given entity index and container.
	 * @param entity The entity index to initialize the iterator with.
	 * @param entity_container The container associated with the iterator.
	 */
	EntityContainerIterator(impl::Index entity, TContainer entity_container) :
		entity_(entity), entity_container_{ entity_container } {
		if (ShouldIncrement()) {
			this->operator++();
		}
		if (!entity_container_.IsMaxEntity(entity_)) {
			ECS_ASSERT(
				entity_container_.EntityWithinLimit(entity_),
				"Cannot create entity container iterator with out-of-range entity "
				"index"
			);
		}
	}

private:
	template <typename T, typename A, bool is_const, impl::LoopCriterion C, typename... S>
	friend class EntityContainer;

	// @brief The current entity index.
	impl::Index entity_{ 0 };

	// @brief The container associated with the iterator.
	TContainer entity_container_;
};

/**
 * @brief EntityContainer provides iteration and access utilities for ECS entities
 * with optional filtering criteria and component access.
 *
 * @tparam T The entity handle type.
 * @tparam is_const Whether this container provides const access.
 * @tparam Criterion Filtering criteria for included entities.
 * @tparam Ts Types of the components accessed through this container.
 */
template <
	typename T, typename Archiver, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
class EntityContainer {
public:
	using ManagerType = std::conditional_t<is_const, const Manager<Archiver>*, Manager<Archiver>*>;

	/** @brief Default constructor */
	EntityContainer() = default;

	/**
	 * @brief Constructs an EntityContainer with the given manager, max entity index, and component
	 * pools.
	 *
	 * @param manager Pointer to the entity manager.
	 * @param max_entity The maximum entity index.
	 * @param pools Pools containing component data.
	 */
	EntityContainer(
		ManagerType manager, impl::Index max_entity,
		const impl::Pools<T, Archiver, is_const, Ts...>& pools
	) :
		manager_{ manager }, max_entity_{ max_entity }, pools_{ pools } {}

	using iterator = EntityContainerIterator<
		Criterion, EntityContainer<T, Archiver, is_const, Criterion, Ts...>&, Ts...>;

	using const_iterator = EntityContainerIterator<
		Criterion, const EntityContainer<T, Archiver, is_const, Criterion, Ts...>&, Ts...>;

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
	 * @tparam IS_CONST Always true; ensures this is only instantiated for const containers.
	 * @param func Function to apply to each entity and its components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	void operator()(const std::function<void(T, const Ts&...)>& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	/**
	 * @brief Invokes a function on each matching entity and its mutable components.
	 * @tparam IS_CONST Always false; ensures this is only instantiated for mutable containers.
	 * @param func Function to apply to each entity and its components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	void operator()(const std::function<void(T, Ts&...)>& func) {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	/**
	 * @brief Applies a function to each matching entity.
	 * @param func Function to apply to each entity.
	 */
	void ForEach(const std::function<void(T)>& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::invoke(func, GetEntity(it.GetEntityId()));
		}
	}

	/**
	 * @brief Returns a vector of all matching entities.
	 * @return A vector containing all entities matching the filtering criteria.
	 */
	[[nodiscard]] std::vector<T> GetVector() const {
		std::vector<T> v;
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
	template <typename A>
	friend class Manager;
	template <impl::LoopCriterion U, typename TContainer, typename... S>
	friend class EntityContainerIterator;

	/** @brief Retrieves an entity object given its index. */
	T GetEntity(impl::Index entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		return T{ entity, manager_->GetVersion(entity), manager_ };
	}

	/** @brief Determines whether the entity meets the loop criterion. */
	[[nodiscard]] bool EntityMeetsCriteria(impl::Index entity) const {
		bool activated{ manager_->IsActivated(entity) };
		if (!activated) {
			return false;
		}
		if constexpr (Criterion == impl::LoopCriterion::None) {
			return true;
		} else {
			if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
				return pools_.Has(entity);
			}
			if constexpr (Criterion == impl::LoopCriterion::WithoutComponents) {
				return pools_.NotHas(entity);
			}
		}
	}

	/** @brief Checks if the entity index equals the maximum. */
	[[nodiscard]] bool IsMaxEntity(impl::Index entity) const {
		return entity == max_entity_;
	}

	/** @brief Checks if the entity index is within valid range. */
	[[nodiscard]] bool EntityWithinLimit(impl::Index entity) const {
		return entity < max_entity_;
	}

	/**
	 * @brief Retrieves a tuple of entity and const references to its components.
	 * @param entity The index of the entity.
	 * @return A tuple containing the entity and its components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(impl::Index entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
			impl::Pools<T, Archiver, true, Ts...> pools{
				manager_->template GetPool<Ts>(manager_->template GetId<Ts>())...
			};
			return pools.GetWithEntity(entity, manager_);
		} else {
			return T{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	/**
	 * @brief Retrieves a tuple of entity and references to its components.
	 * @param entity The index of the entity.
	 * @return A tuple containing the entity and its mutable components.
	 */
	template <bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(impl::Index entity) {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
			impl::Pools<T, Archiver, false, Ts...> pools{
				manager_->template GetPool<Ts>(manager_->template GetId<Ts>())...
			};
			return pools.GetWithEntity(entity, manager_);
		} else {
			return T{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	// @brief Pointer to the ECS manager.
	ManagerType manager_{ nullptr };

	// @brief Maximum valid entity index.
	impl::Index max_entity_{ 0 };

	// @brief Pools of components managed by this container.
	impl::Pools<T, Archiver, is_const, Ts...> pools_;
};

namespace impl {

template <typename T, typename Archiver, bool is_const, typename... Ts>
[[nodiscard]] constexpr decltype(auto) Pools<T, Archiver, is_const, Ts...>::GetWithEntity(
	Index entity, const Manager<Archiver>* manager
) const {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(Ts) > 0);
	return std::tuple<T, const Ts&...>(
		T{ entity, manager->GetVersion(entity), manager },
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity))...
	);
}

template <typename T, typename Archiver, bool is_const, typename... Ts>
[[nodiscard]] constexpr decltype(auto) Pools<T, Archiver, is_const, Ts...>::GetWithEntity(
	Index entity, Manager<Archiver>* manager
) {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(Ts) > 0);
	return std::tuple<T, Ts&...>(
		T{ entity, manager->GetVersion(entity), manager },
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts, Archiver>::Get(entity))...
	);
}

} // namespace impl

template <typename Archiver>
inline Entity<Archiver> Manager<Archiver>::CreateEntity() {
	impl::Index entity{ 0 };
	impl::Version version{ 0 };
	GenerateEntity(entity, version);
	ECS_ASSERT(version != 0, "Failed to create new entity in manager");
	return Entity{ entity, version, this };
}

template <typename Archiver>
template <typename... Ts>
inline void Manager<Archiver>::CopyEntity(const Entity<Archiver>& from, Entity<Archiver>& to) {
	CopyEntity<Ts...>(from.entity_, from.version_, to.entity_, to.version_);
}

template <typename Archiver>
template <typename... Ts>
inline Entity<Archiver> Manager<Archiver>::CopyEntity(const Entity<Archiver>& from) {
	Entity<Archiver> to{ CreateEntity() };
	CopyEntity<Ts...>(from, to);
	return to;
}

template <typename Archiver>
template <typename... Ts>
inline ecs::EntitiesWith<Archiver, true, Ts...> Manager<Archiver>::EntitiesWith() const {
	return { this, next_entity_,
			 impl::Pools<Entity<Archiver>, Archiver, true, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename Archiver>
template <typename... Ts>
inline ecs::EntitiesWith<Archiver, false, Ts...> Manager<Archiver>::EntitiesWith() {
	return { this, next_entity_,
			 impl::Pools<Entity<Archiver>, Archiver, false, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename Archiver>
template <typename... Ts>
inline ecs::EntitiesWithout<Archiver, true, Ts...> Manager<Archiver>::EntitiesWithout() const {
	return { this, next_entity_,
			 impl::Pools<Entity<Archiver>, Archiver, true, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename Archiver>
template <typename... Ts>
inline ecs::EntitiesWithout<Archiver, false, Ts...> Manager<Archiver>::EntitiesWithout() {
	return { this, next_entity_,
			 impl::Pools<Entity<Archiver>, Archiver, false, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename Archiver>
inline ecs::Entities<Archiver, true> Manager<Archiver>::Entities() const {
	return { this, next_entity_, impl::Pools<Entity<Archiver>, Archiver, true>{} };
}

template <typename Archiver>
inline ecs::Entities<Archiver, false> Manager<Archiver>::Entities() {
	return { this, next_entity_, impl::Pools<Entity<Archiver>, Archiver, false>{} };
}

template <typename Archiver>
inline void Manager<Archiver>::ClearEntities() {
	for (auto entity : Entities()) {
		entity.Destroy();
	}
}

} // namespace ecs

namespace std {

/**
 * @brief Specialization of the std::hash template for the ecs::Entity type.
 *
 * This specialization provides a custom hash function for the `ecs::Entity` type.
 * It combines the hashes of the associated manager, entity index, and version into a single hash
 * value. This is used when an `ecs::Entity` is used as a key in hash-based containers such as
 * `std::unordered_map`.
 *
 * @tparam ecs::Entity The type for which the hash is being specialized.
 */
template <typename Archiver>
struct hash<ecs::Entity<Archiver>> {
	/**
	 * @brief Computes the hash value for an ecs::Entity object.
	 *
	 * This function combines the individual hash values of the entity's manager, entity index, and
	 * version. The resulting hash value is then returned.
	 *
	 * @param e The `ecs::Entity` object for which the hash value is being calculated.
	 * @return The computed hash value for the given `ecs::Entity`.
	 */
	size_t operator()(const ecs::Entity<Archiver>& e) const {
		// Source: https://stackoverflow.com/a/17017281
		size_t h{ 17 };
		h = h * 31 + hash<ecs::Manager<Archiver>*>()(e.manager_
					 );								/**< Hash for the associated manager pointer. */
		h = h * 31 +
			hash<ecs::impl::Index>()(e.entity_);	/**< Hash for the entity's unique index. */
		h = h * 31 +
			hash<ecs::impl::Version>()(e.version_); /**< Hash for the entity's version number. */
		return h;									/**< Final combined hash value. */
	}
};

} // namespace std