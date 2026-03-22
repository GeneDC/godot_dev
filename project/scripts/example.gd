extends Node

var chunk_loader : ChunkLoader
var chunk_positions: Array[Vector3i] = []
var is_init: bool = false

@export var player_view_control: ViewControl
@export var free_cam_view_control: ViewControl

func _ready() -> void:
	call_deferred("init")

func _exit_tree() -> void:
	if chunk_loader.can_stop():
		chunk_loader.stop()

func init() -> void:
	chunk_loader = get_node("ChunkLoader")
	if (chunk_loader.can_init()):
		chunk_loader.init()

func _process(_delta_time: float) -> void:
	if (chunk_loader.can_update()):
		chunk_loader.update()

func _unhandled_input(_event: InputEvent) -> void:
	if Input.is_action_just_pressed("toggle_free_cam"):
		if player_view_control.is_view_active():
			free_cam_view_control.activate()
			player_view_control.deactivate()
		else:
			player_view_control.activate()
			free_cam_view_control.deactivate()

func _unhandled_key_input(event: InputEvent) -> void:
	var event_key := event as InputEventKey
	if (!event_key.pressed):
		pass

	if (event_key.keycode == KEY_L):
		if chunk_loader.can_stop():
			chunk_loader.stop()
		elif chunk_loader.can_init():
			chunk_loader.init()

	if (event_key.keycode == KEY_R):
		if chunk_loader.can_update():
			chunk_loader.unload_all()
