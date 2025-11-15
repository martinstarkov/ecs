#pragma once

#include <vector>

template <typename T>
struct SparseDense {
	std::vector<std::size_t> dense;
	std::vector<std::size_t> sparse;
	std::vector<T> data;

	SparseDense(std::size_t max = 0) : sparse(max, SIZE_MAX) {}

	void add(std::size_t id, T&& c) {
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

	void remove(std::size_t id) {
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
};
