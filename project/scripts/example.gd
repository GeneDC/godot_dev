extends Node

var chunk_loader : ChunkLoader
var chunk_positions: Array[Vector3i] = []
var is_init: bool = false

func _ready() -> void:
	call_deferred("init")

func _exit_tree():
	if chunk_loader.can_stop():
		chunk_loader.stop()
	
func init() -> void:
	chunk_loader = get_node("ChunkLoader")
	if (chunk_loader.can_init()):
		chunk_loader.init()
	
func _process(_delta_time: float) -> void:
	if (chunk_loader.can_update()):
		chunk_loader.update()
		
func _input(event: InputEvent):
	if (event is InputEventKey and event.pressed):
		var eventkey := event as InputEventKey
		
		if (eventkey.keycode == KEY_L):
			if chunk_loader.can_stop():
				chunk_loader.stop()
			elif chunk_loader.can_init():
				chunk_loader.init()
				
		if (eventkey.keycode == KEY_R):
			if chunk_loader.can_update():
				chunk_loader.unload_all()
