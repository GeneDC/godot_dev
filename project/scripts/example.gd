extends Node

var chunk_loader : ChunkLoader
var chunk_positions: Array[Vector3i] = []

func _ready() -> void:
	call_deferred("init")

func _exit_tree():
	chunk_loader.stop()
	
func init() -> void:
	chunk_loader = get_node("ChunkLoader")
	chunk_loader.init()
	chunk_loader.update_chunks(Vector3i.ZERO, 10)
	
func _process(_delta_time: float) -> void:
	chunk_loader.update()
