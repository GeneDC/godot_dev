extends Node

func find_sibling_of_type(node: Node, type: Variant) -> Node:
	var parent := node.get_parent()
	if not parent:
		return null
		
	for child in parent.get_children():
		if is_instance_of(child, type) and child != self:
			return child
	return null
