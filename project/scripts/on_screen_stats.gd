extends CanvasLayer

@onready var label = $Label

var monitors: Dictionary[String, Callable] = {}

func _ready():
	add_perf_monitor("FPS", func(): return Performance.get_monitor(Performance.TIME_FPS))
	add_perf_monitor("Static Mem", func(): return "%.2f MB" % (Performance.get_monitor(Performance.MEMORY_STATIC) / 1024 / 1024))
	add_perf_monitor("Draw Calls", func(): return "%.0f" % Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	add_perf_monitor("Chunks", func(): return Performance.get_custom_monitor("Terrain/LoadedChunkCount"))
	add_perf_monitor("CPS", func(): return "%.1f/s" % Performance.get_custom_monitor("Terrain/LoadedChunkCountPerSec"))
	add_perf_monitor("Mesh Tasks", func(): return "%.2f%%" % (100 * Performance.get_custom_monitor("Terrain/MeshTasksPerSec")))
	add_perf_monitor("Pending Chunks", func(): return Performance.get_custom_monitor("Terrain/PendingChunks"))
	add_perf_monitor("Done Mesh Datas", func(): return Performance.get_custom_monitor("Terrain/DoneMeshDatas"))
	

# TODO: Make this a AutoLoad, and call this from anywhere
func add_perf_monitor(display_name: String, getter_callable: Callable):
	monitors[display_name] = getter_callable

# Make sure to remove a monitor when the object dies
func remove_perf_monitor(display_name: String):
	monitors.erase(display_name)

func _process(_delta):
	if !visible:
		pass
	
	var display_text = ""
	for key in monitors:
		var value = monitors[key].call()
		display_text += str(key, ": ", value, "\n")
	
	label.text = display_text

func _input(event):
	# Toggles visibility when F3 is pressed
	if (event is InputEventKey and event.keycode == KEY_F3 and event.pressed):
		visible = !visible
