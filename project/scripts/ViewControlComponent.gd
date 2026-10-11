extends Node
class_name ViewControlComponent

var camera: Camera3D
var input_controller: InputControllerComponent
@export var activate_on_ready := false
@export var mouse_mode := Input.MOUSE_MODE_CAPTURED

var is_active := false

func _ready() -> void:
	assert(get_parent(), "ViewControlComponent must be a child node")
	
	if not camera:
		camera = get_parent() as Camera3D
	if not camera:
		camera = GGUtils.find_sibling_of_type(self, Camera3D) as Camera3D
	
	if not input_controller:
		input_controller = GGUtils.find_sibling_of_type(self, InputControllerComponent) as InputControllerComponent
	
	call_deferred("set_active", activate_on_ready)

func activate() -> void:
	set_active(true)

func deactivate() -> void:
	set_active(false)

func set_active(active: bool) -> void:
	if active:
		if camera:
			camera.make_current()
		Input.set_mouse_mode(mouse_mode)
	if input_controller:
		input_controller.set_active(active)
	
	is_active = active
