/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Paths between two nodes of a tree: CIX's tree_find_lowest_common_parent
 * and tree_traverse_path (Source/common/.../u_tree.c), translated.  The PLL
 * policy walks its state machine with them (pll_policy.c): leaving a state
 * exits every node up to the common parent, entering one enters every node
 * from there down.
 */
#include "clk.h"

/*
 * The deepest node that is an ancestor of both (or one of them); false if
 * either is NULL or they are in different trees.
 */
bool tree_find_lowest_common_parent(TREE_NODE *node_a, TREE_NODE *node_b,
				    TREE_NODE **common_parent)
{
	if (node_a == NULL || node_b == NULL)
		return false;
	while (node_a->depth > node_b->depth)
		node_a = node_a->parent;
	while (node_b->depth > node_a->depth)
		node_b = node_b->parent;
	while (node_a != node_b) {
		node_a = node_a->parent;
		node_b = node_b->parent;
	}
	if (node_a == NULL)
		return false;
	if (common_parent)
		*common_parent = node_a;
	return true;
}

/* visit_down on the path from below common down to node, top first */
static bool visit_path_down(TREE_NODE *node, TREE_NODE *common, void *data,
			    bool (*visit_down)(TREE_NODE *, void *))
{
	if (node == common)
		return true;
	if (!visit_path_down(node->parent, common, data, visit_down))
		return false;
	return visit_down(node, data);
}

/*
 * From node_a up to (not including) the common parent with visit_up, the
 * common parent with visit_parent, then down to node_b with visit_down.
 * Each visitor may be NULL; the walk stops at the first visit that returns
 * false.  CIX collects the downward path in a stack array (alloca) and
 * visits it backwards; the recursion visits the same nodes in the same
 * order.
 */
bool tree_traverse_path(TREE_NODE *node_a, TREE_NODE *node_b, void *data,
			bool (*visit_up)(TREE_NODE *, void *),
			bool (*visit_down)(TREE_NODE *, void *),
			bool (*visit_parent)(TREE_NODE *, void *))
{
	TREE_NODE *common = NULL, *n;

	if (!tree_find_lowest_common_parent(node_a, node_b, &common))
		return false;
	if (visit_up) {
		for (n = node_a; n != common; n = n->parent) {
			if (!visit_up(n, data))
				return false;
		}
	}
	if (visit_parent && !visit_parent(common, data))
		return false;
	if (visit_down == NULL)
		return true;
	return visit_path_down(node_b, common, data, visit_down);
}
