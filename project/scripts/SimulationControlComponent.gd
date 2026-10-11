extends Node
class_name SimulationControlComponent

var character_body: CharacterBody3D
@export var activate_on_ready := true

var is_active := false

func _ready() -> void:
	assert(get_parent(), "SimulationControlComponent must be a child node")
	
	if not character_body:
		character_body = get_parent() as CharacterBody3D
	
	call_deferred("set_active", activate_on_ready)

func activate() -> void:
	set_active(true)

func deactivate() -> void:
	set_active(false)

func set_active(active: bool) -> void:
	if character_body:
		character_body.set_physics_process(active)
		if (!is_active && active):
			character_body.velocity = Vector3.ZERO
	
	is_active = active
