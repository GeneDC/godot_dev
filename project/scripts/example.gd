extends Node

var chunk_loader : ChunkLoader
var chunk_positions: Array[Vector3i] = []
var is_init: bool = false

func _ready() -> void:
	call_deferred("init")

func _exit_tree():
	chunk_loader.stop()
	
func init() -> void:
	chunk_loader = get_node("ChunkLoader")
	is_init = chunk_loader.init()
	
func _process(_delta_time: float) -> void:
	if (is_init):
		chunk_loader.update()
