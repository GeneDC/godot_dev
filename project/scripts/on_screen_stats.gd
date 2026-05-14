extends CanvasLayer

@onready var label: Label = $Label

var monitors: Dictionary[String, Callable] = {}

func _ready() -> void:
	add_perf_monitor("FPS", func() -> float: return Performance.get_monitor(Performance.TIME_FPS))
	add_perf_monitor("Static Mem", func() -> String: return "%.2f MB" % (Performance.get_monitor(Performance.MEMORY_STATIC) / 1024 / 1024))
	add_perf_monitor("Draw Calls", func() -> String: return "%.0f" % Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	add_perf_monitor("Chunks", func() -> float: return Performance.get_custom_monitor("Terrain/LoadedChunkCount"))
	add_perf_monitor("Chunks (Pooled)", func() -> float: return Performance.get_custom_monitor("Terrain/PooledChunkCount"))
	add_perf_monitor("CPS", func() -> String: return "%.1f/s" % Performance.get_custom_monitor("Terrain/LoadedChunkCountPerSec"))
	add_perf_monitor("Mesh Tasks", func() -> String: return "%.2f%%" % (100 * Performance.get_custom_monitor("Terrain/MeshTasksPerSec")))
	add_perf_monitor("Pending Chunks", func() -> float: return Performance.get_custom_monitor("Terrain/PendingChunks"))
	add_perf_monitor("Done Mesh Datas", func() -> float: return Performance.get_custom_monitor("Terrain/DoneMeshDatas"))

# TODO: Make this a AutoLoad, and call this from anywhere
func add_perf_monitor(display_name: String, getter_callable: Callable) -> void:
	monitors[display_name] = getter_callable

# Make sure to remove a monitor when the object dies
func remove_perf_monitor(display_name: String) -> void:
	monitors.erase(display_name)

func _process(_delta: float) -> void:
	if !visible:
		pass
	
	var display_text := ""
	for key in monitors:
		var value: Variant = monitors[key].call()
		display_text += str(key, ": ", value, "\n")
	
	var chunk_loader := GlobalChunkLoader as ChunkLoader
	if chunk_loader:
		var chunk_viewer := chunk_loader.get_chunk_viewer()
		if chunk_viewer:
			display_text += str("Chunk Coord: ", chunk_viewer.get_current_chunk_pos(), "\n")
	
	label.text = display_text

func _input(event: InputEvent) -> void:
	# Toggles visibility when F3 is pressed
	if (event is InputEventKey):
		var event_key := event as InputEventKey
		if (event_key.keycode == KEY_F3 and event_key.pressed):
			visible = !visible
