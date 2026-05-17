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
	chunk_loader = GlobalChunkLoader as ChunkLoader
	if (chunk_loader.can_init()):
		chunk_loader.init()

func _process(_delta_time: float) -> void:
	if (chunk_loader.can_update()):
		chunk_loader.update()
	_debug_update_grid()

func _unhandled_input(_event: InputEvent) -> void:
	if Input.is_action_just_pressed("toggle_free_cam"):
		if player_view_control.is_view_active():
			free_cam_view_control.activate()
			player_view_control.deactivate()
		else:
			player_view_control.activate()
			free_cam_view_control.deactivate()

var _f3_combo_used := false
func _unhandled_key_input(event: InputEvent) -> void:
	var event_key := event as InputEventKey
	if not event_key:
		return
	if event_key.is_echo():
		return

	if (event_key.is_released() and Input.is_key_pressed(KEY_F3)):
		# F3+L: Toggle Chunk Loading
		if (event_key.keycode == KEY_L):
			if chunk_loader.can_stop():
				chunk_loader.stop()
			elif chunk_loader.can_init():
				chunk_loader.init()
			
			_f3_combo_used = true
			get_viewport().set_input_as_handled()
			return
			
		# F3+A: Unload all chunks
		elif (event_key.keycode == KEY_A):
			if chunk_loader.can_update():
				chunk_loader.unload_all()
			
			_f3_combo_used = true
			get_viewport().set_input_as_handled()
			return
			
		# F3+G: Toggle chunk boarders
		elif (event_key.keycode == KEY_G):
			_debug_toggle_grid()
			
			_f3_combo_used = true
			get_viewport().set_input_as_handled()
			return
			
	# F3: Toggle debug info
	if (event_key.keycode == KEY_F3 and event_key.is_released()):
		if (not _f3_combo_used):
			var debug_info: DebugInfo = GlobalDebugInfo
			debug_info.toggle_visibility()
			get_viewport().set_input_as_handled()
			
		_f3_combo_used = false

var debug_grid_mesh_instance: MeshInstance3D
var debug_grid_mesh: ImmediateMesh
var debug_grid_material: ShaderMaterial = preload("res://Materials/DebugGridMaterial.tres")
const chunk_size := 32
const debug_grid_distance := 4 * chunk_size

func _debug_toggle_grid() -> void:
	if (debug_grid_mesh_instance):
		debug_grid_mesh_instance.set_visible(!debug_grid_mesh_instance.is_visible())
	else:
		# It was never shown so set it up
		_debug_show_grid()

func _debug_update_grid() -> void:
	var chunk_viewer := chunk_loader.get_chunk_viewer()
	if (not chunk_viewer 
		or not debug_grid_mesh_instance 
		or not debug_grid_mesh_instance.is_visible()):
		return
	
	var chunk_coord := chunk_viewer.get_current_chunk_pos()
	chunk_coord += Vector3i.BACK
	#chunk_coord += Vector3i.LEFT
	var chunk_position: Vector3 = chunk_coord * chunk_size
	debug_grid_mesh_instance.global_position = chunk_position

func _debug_show_grid() -> void:
	if (!debug_grid_mesh_instance):
		debug_grid_mesh_instance = MeshInstance3D.new()
		debug_grid_mesh_instance.name = "DebugGridMesh"
		debug_grid_mesh_instance.material_override = debug_grid_material
		
		add_child(debug_grid_mesh_instance)
		debug_grid_mesh = ImmediateMesh.new()
		debug_grid_mesh_instance.mesh = debug_grid_mesh
		
		debug_grid_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
		
		# z
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.ZERO, debug_grid_distance)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.RIGHT, debug_grid_distance)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.UP, debug_grid_distance)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.RIGHT + Vector3.UP, debug_grid_distance)
		
		# x
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.ZERO, debug_grid_distance)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.FORWARD, debug_grid_distance)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.UP, debug_grid_distance)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.FORWARD + Vector3.UP, debug_grid_distance)
		
		# y
		_debug_draw_grid_line(Vector3.UP, Vector3.ZERO, debug_grid_distance)
		_debug_draw_grid_line(Vector3.UP, Vector3.FORWARD, debug_grid_distance)
		_debug_draw_grid_line(Vector3.UP, Vector3.RIGHT, debug_grid_distance)
		_debug_draw_grid_line(Vector3.UP, Vector3.FORWARD + Vector3.RIGHT, debug_grid_distance)
		
		# y plane
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.FORWARD, Vector3.RIGHT)
		_debug_draw_grid_plane(Vector3.UP * chunk_size, Vector3.FORWARD, Vector3.RIGHT)
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.RIGHT, Vector3.FORWARD)
		_debug_draw_grid_plane(Vector3.UP * chunk_size, Vector3.RIGHT, Vector3.FORWARD)
		
		# x plane
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.UP, Vector3.RIGHT)
		_debug_draw_grid_plane(Vector3.FORWARD * chunk_size, Vector3.UP, Vector3.RIGHT)
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.RIGHT, Vector3.UP)
		_debug_draw_grid_plane(Vector3.FORWARD * chunk_size, Vector3.RIGHT, Vector3.UP)
		
		# z plane
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.UP, Vector3.FORWARD)
		_debug_draw_grid_plane(Vector3.RIGHT * chunk_size, Vector3.UP, Vector3.FORWARD)
		_debug_draw_grid_plane(Vector3.ZERO, Vector3.FORWARD, Vector3.UP)
		_debug_draw_grid_plane(Vector3.RIGHT * chunk_size, Vector3.FORWARD, Vector3.UP)
		
		debug_grid_mesh.surface_end()
	
	debug_grid_mesh_instance.visible = true

func _debug_draw_grid_line(axis: Vector3, position: Vector3, distance: float) -> void:
	debug_grid_mesh.surface_set_normal(axis)
	debug_grid_mesh.surface_add_vertex(position * chunk_size + -axis * distance)
	debug_grid_mesh.surface_set_normal(axis)
	debug_grid_mesh.surface_add_vertex(position * chunk_size + axis * distance)
	
func _debug_draw_grid_plane(origin: Vector3, axisA: Vector3, axisB: Vector3) -> void:
	const subdivisions := 3
	const dist_between_lines := chunk_size >> subdivisions
	const line_count := 1 << subdivisions
	for i in line_count:
		var position := origin + (axisA * (i + 1) * dist_between_lines)
		debug_grid_mesh.surface_set_normal(axisB)
		debug_grid_mesh.surface_add_vertex(position)
		debug_grid_mesh.surface_set_normal(axisB)
		debug_grid_mesh.surface_add_vertex(position + axisB * chunk_size)
