#ifndef bvh_hpp
#define bvh_hpp

#pragma once

#include "geometric.hpp"
#include <vector>
#include <memory>

using namespace avl;

class BVH
{

	struct ObjectComparator
	{
		int split;
		ObjectComparator(const int axis = 0) : split(axis) { }
		bool operator()(const Traceable * a, const Traceable * b) const { return true; }
	};

	std::vector<std::shared_ptr<Traceable>> objects;
	bool initialized = false;
public:

	BVH(std::vector<std::shared_ptr<Traceable>> objects) : objects(objects) {}
	~BVH() {}

	void build()
	{
		// ... 
		initialized = true;
	}

	// Compute entire bounds of BVH in world space
	Bounds3D world_space() const
	{

	}

	RayIntersection intersect(const Ray & ray)
	{

	}

	RayIntersection intersect_p(const Ray & ray)
	{

	}
};

#endif