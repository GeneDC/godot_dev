#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/memory.hpp>

#include <concepts>
#include <vector>

template <typename T>
concept PoolableNode = requires(T * t)
{
	{
		t->on_pool_acquire()
	} -> std::same_as<void>;
	{
		t->on_pool_release()
	} -> std::same_as<void>;
};

template <typename TNode>
requires std::derived_from<TNode, godot::Node> class NodePool
{
public:
	NodePool() = default;
	NodePool(godot::Node* p_parent_node) :
			parent_node(p_parent_node) {}
	~NodePool() { clear(); }

	NodePool(const NodePool&) = delete;
	NodePool& operator=(const NodePool&) = delete;
	NodePool(NodePool&&) = default;
	NodePool& operator=(NodePool&&) = default;

	template <typename TFactory>
	void preallocate(int p_count, TFactory&& factory)
	{
		pool.reserve(pool.size() + p_count);
		for (size_t i = 0; i < p_count; ++i)
		{
			TNode* node = factory();
			if (parent_node)
			{
				parent_node->add_child(node);
			}
			reset_node(node);
			pool.push_back(node);
		}
	}

	void preallocate(int p_count)
	{
		preallocate(p_count, []()
				{ return memnew(TNode); });
	}

	template <typename TFactory>
	TNode* acquire(TFactory&& factory)
	{
		TNode* node = nullptr;
		if (!pool.empty())
		{
			node = pool.back();
			pool.pop_back();
		}
		else
		{
			node = factory();
			if (parent_node)
			{
				parent_node->add_child(node);
			}
		}

		prepare_node(node);
		return node;
	}

	TNode* acquire()
	{
		return acquire(
				[]()
				{
					return memnew(TNode);
				});
	}

	void release(TNode* p_node)
	{
		if (!p_node)
		{
			return;
		}

		reset_node(p_node);
		pool.push_back(p_node);
	}

	void clear()
	{
		for (TNode* node : pool)
		{
			if (node)
			{
				node->queue_free();
			}
		}
		pool.clear();
	}

	int get_available_count() const { return pool.size(); }

private:
	void prepare_node(TNode* node)
	{
		if constexpr (PoolableNode<TNode>)
		{
			node->on_pool_acquire();
		}
	}

	void reset_node(TNode* node)
	{
		if constexpr (PoolableNode<TNode>)
		{
			node->on_pool_release();
		}
	}

	godot::Node* parent_node = nullptr;
	std::vector<TNode*> pool{};
};
