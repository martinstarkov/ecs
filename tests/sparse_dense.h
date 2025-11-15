#pragma once

#include <vector>

template <typename T>
struct SparseDense {
	std::vector<std::size_t> dense;
	std::vector<std::size_t> sparse;
	std::vector<T> data;

	SparseDense(std::size_t max = 0) : sparse(max, SIZE_MAX) {}

	void Add(std::size_t id, T&& c) {
		if (id >= sparse.size()) {
			sparse.resize(id + 1, SIZE_MAX);
		}

		if (sparse[id] == SIZE_MAX) { // new
			sparse[id] = dense.size();
			dense.push_back(id);
			data.push_back(c);
		} else { // overwrite
			data[sparse[id]] = c;
		}
	}

	void Remove(std::size_t id) {
		if (id >= sparse.size() || sparse[id] == SIZE_MAX) {
			return;
		}

		std::size_t idx	 = sparse[id];
		std::size_t last = dense.back();

		dense[idx] = last;
		data[idx]  = data.back();

		sparse[last] = idx;
		sparse[id]	 = SIZE_MAX;

		dense.pop_back();
		data.pop_back();
	}

	// Returns true if the id exists in the set
	bool Contains(std::size_t id) const {
		return id < sparse.size() && sparse[id] != SIZE_MAX;
	}

	// Safe retrieval: returns pointer or nullptr
	const T* Get(std::size_t id) const {
		return Contains(id) ? &data[sparse[id]] : nullptr;
	}

	T* Get(std::size_t id) {
		return Contains(id) ? &data[sparse[id]] : nullptr;
	}
};
