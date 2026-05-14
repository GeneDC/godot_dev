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
	_debug_show_grid()

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

var grid_size := Vector3i(1, 1, 1)
var debug_grid_mesh_instance: MeshInstance3D
var debug_grid_mesh: ImmediateMesh
var debug_grid_material: ShaderMaterial = preload("res://Materials/DebugGridMaterial.tres")
var debug_grid_distance := 4

func _debug_update_grid() -> void:
	var chunk_viewer := chunk_loader.get_chunk_viewer()
	if (not chunk_viewer or not debug_grid_mesh_instance):
		return
	
	var chunk_size := 32
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
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.ZERO)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.RIGHT)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.UP)
		_debug_draw_grid_line(Vector3.FORWARD, Vector3.RIGHT + Vector3.UP)
		
		# x
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.ZERO)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.FORWARD)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.UP)
		_debug_draw_grid_line(Vector3.RIGHT, Vector3.FORWARD + Vector3.UP)
		
		# y
		_debug_draw_grid_line(Vector3.UP, Vector3.ZERO)
		_debug_draw_grid_line(Vector3.UP, Vector3.FORWARD)
		_debug_draw_grid_line(Vector3.UP, Vector3.RIGHT)
		_debug_draw_grid_line(Vector3.UP, Vector3.FORWARD + Vector3.RIGHT)
		
		debug_grid_mesh.surface_end()
		
		var chunk_size := 32
		debug_grid_mesh_instance.scale = Vector3.ONE * chunk_size
	
	debug_grid_mesh_instance.visible = true

func _debug_draw_grid_line(normal: Vector3, position: Vector3) -> void:
	debug_grid_mesh.surface_set_normal(normal)
	debug_grid_mesh.surface_add_vertex(position + -normal * debug_grid_distance)
	debug_grid_mesh.surface_set_normal(normal)
	debug_grid_mesh.surface_add_vertex(position + normal * debug_grid_distance)
