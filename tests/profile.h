#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "ecs/ecs.h"
#include "sparse_dense.h"

std::vector<std::size_t> BENCHMARK_COUNTS{ 100'000, 1'000'000, 5'000'000 };

struct ProfileTestComponent {
	int x{ 0 }, y{ 0 };
};

template <typename Fn>
long long Time(Fn&& fn) {
	auto start = std::chrono::high_resolution_clock::now();
	fn();
	auto stop = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
}

inline std::string FormatWithCommas(std::size_t value) {
	std::string s = std::to_string(value);
	int n		  = s.length() - 3;
	while (n > 0) {
		s.insert(n, ",");
		n -= 3;
	}
	return s;
}

inline void PrintHeader(std::size_t count) {
	Print("\n========================================");
	Print("Running ECS Benchmarks with ", FormatWithCommas(count), " entities");
	Print("========================================\n");
}

inline void PrintTimeCompact(const char* label, long long ms) {
	// Pad label to a fixed width (30 chars)
	constexpr int width = 30;
	std::string s(label);
	if (s.size() < width) {
		s.append(width - s.size(), '.');
	}

	Print(s, ms, " ms");
}

void ProfileECS(std::size_t count) {
	ecs::Manager m;

	PrintTimeCompact("Creating", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 m.CreateEntity();
						 }
						 m.Refresh();
					 }));

	PrintTimeCompact("Adding components", Time([&] {
						 for (auto e : m.Entities()) {
							 e.Add<ProfileTestComponent>(3, 3);
						 }
					 }));

	PrintTimeCompact("Incrementing components", Time([&] {
						 for (auto [e, c] : m.EntitiesWith<ProfileTestComponent>()) {
							 c.x += 1;
						 }
					 }));

	PrintTimeCompact("Removing components", Time([&] {
						 for (auto e : m.Entities()) {
							 e.Remove<ProfileTestComponent>();
						 }
					 }));

	PrintTimeCompact("Re-add components 2x", Time([&] {
						 for (auto e : m.Entities()) {
							 e.Add<ProfileTestComponent>(4, 4);
						 }
						 for (auto e : m.Entities()) {
							 e.Add<ProfileTestComponent>(5, 5);
						 }
					 }));

	PrintTimeCompact("Erasing", Time([&] {
						 for (auto [e, c] : m.EntitiesWith<ProfileTestComponent>()) {
							 e.Destroy();
						 }
						 m.Refresh();
					 }));
}

void ProfileUnorderedMap(std::size_t count) {
	using Entity = std::size_t;
	std::unordered_map<Entity, ProfileTestComponent> map;

	PrintTimeCompact("Creating", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 map.emplace(i, ProfileTestComponent{});
						 }
					 }));

	PrintTimeCompact("Adding components", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 map[i] = ProfileTestComponent{ 3, 3 };
						 }
					 }));

	PrintTimeCompact("Incrementing components", Time([&] {
						 for (auto& [id, comp] : map) {
							 comp.x += 1;
						 }
					 }));

	PrintTimeCompact("Removing components", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 map.erase(i);
						 }
					 }));

	PrintTimeCompact("Re-add components 2x", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 map[i] = ProfileTestComponent{ 4, 4 };
						 }
						 for (std::size_t i = 0; i < count; i++) {
							 map[i] = ProfileTestComponent{ 5, 5 };
						 }
					 }));

	PrintTimeCompact("Erasing", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 map.erase(i);
						 }
					 }));
}

void ProfileSparseDense(std::size_t count) {
	SparseDense<ProfileTestComponent> sd(count);

	PrintTimeCompact("Creating", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Add(i, ProfileTestComponent{});
						 }
					 }));

	PrintTimeCompact("Adding components", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Add(i, ProfileTestComponent{ 3, 3 });
						 }
					 }));

	PrintTimeCompact("Incrementing components", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 if (sd.Contains(i)) {
								 sd.Get(i)->x += 1;
							 }
						 }
					 }));

	PrintTimeCompact("Removing components", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Remove(i);
						 }
					 }));

	PrintTimeCompact("Re-add components 2x", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Add(i, ProfileTestComponent{ 4, 4 });
						 }
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Add(i, ProfileTestComponent{ 5, 5 });
						 }
					 }));

	PrintTimeCompact("Erasing", Time([&] {
						 for (std::size_t i = 0; i < count; i++) {
							 sd.Remove(i);
						 }
					 }));
}

void RunBenchmarks(const std::vector<std::size_t>& counts) {
	for (std::size_t count : counts) {
		PrintHeader(count);

		Print("===== Unordered Map =====");
		ProfileUnorderedMap(count);

		Print("\n===== Sparse-Dense =====");
		ProfileSparseDense(count);

		Print("\n===== ECS =====");
		ProfileECS(count);

		Print("\n");
	}
}

void ProfileECS() {
	RunBenchmarks(BENCHMARK_COUNTS);
}
