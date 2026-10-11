extends Node
class_name InputControllerComponent

var target_node: Node
@export var active_on_ready := false

var is_active := false

func _ready() -> void:
	assert(get_parent(), "SimulationControlComponent must be a child node")
	
	if not target_node:
		target_node = get_parent()
	
	set_active(active_on_ready)

func activate() -> void:
	set_active(true)

func deactivate() -> void:
	set_active(false)

func set_active(active: bool) -> void:
	if target_node:
		target_node.set_process_input(active)
		target_node.set_process_unhandled_input(active)
	
	is_active = active
