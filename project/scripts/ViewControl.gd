extends Node
class_name ViewControl

@export var camera: Camera3D
@export var controlled_node: Node3D
@export var activate_on_ready := false
@export var toggle_physics_process := false
@export var toggle_process_unhandled_input := true
@export var mouse_mode := Input.MOUSE_MODE_CAPTURED

var is_active := false

func _ready() -> void:
	if camera == null:
		push_error("Missing required @export: 'camera' is not assigned!")
	if (toggle_physics_process || toggle_process_unhandled_input) && controlled_node == null:
		push_error("Missing required @export: 'controlled_node' is not assigned!")

	# Use call_deferred to ensure it's set after _ready on parent nodes
	if activate_on_ready:
		call_deferred("activate")
	else:
		call_deferred("deactivate")

func is_view_active() -> bool:
	return is_active

func activate() -> void:
	is_active = true

	if (camera):
		camera.make_current()

	if (controlled_node):
		if (toggle_physics_process):
			controlled_node.set_physics_process(true)
		if (toggle_process_unhandled_input):
			controlled_node.set_process_input(true)
			controlled_node.set_process_unhandled_input(true)

	Input.mouse_mode = mouse_mode

func deactivate() -> void:
	is_active = false

	if (controlled_node):
		if (toggle_physics_process):
			controlled_node.set_physics_process(false)
		if (toggle_process_unhandled_input):
			controlled_node.set_process_input(false)
			controlled_node.set_process_unhandled_input(false)
