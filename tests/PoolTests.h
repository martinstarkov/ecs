#pragma once

#include "common.h"

inline void PoolTests() {
	auto pool = ecs::internal::Pool<int>();
	assert(!pool.Has(0));
	pool.Add(0, 4);
	auto* num = pool.Get(0);
	assert(num != nullptr);
	assert(*num == 4);
	assert(pool.Has(0));
	pool.Remove(0);
	num = pool.Get(0);
	assert(num == nullptr);
	assert(!pool.Has(0));
	pool.Add(0, 3);
	num = pool.Get(0);
	assert(num != nullptr);
	assert(*num == 3);
	auto new_pool = static_cast<ecs::internal::Pool<int>*>(pool.Clone());
	assert(new_pool->Hash() == pool.Hash());
	new_pool->Add(1, 5);
	assert(new_pool->Has(1));
	assert(!pool.Has(1));
	assert(new_pool->Hash() != pool.Hash());
	pool.Remove(1);
	assert(new_pool->Hash() != pool.Hash());
	pool.Clear();
	new_pool->Clear();
	assert(!new_pool->Has(0));
	assert(new_pool->Hash() != pool.Hash());
	delete new_pool;
}