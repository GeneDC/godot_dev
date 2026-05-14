extends CharacterBody3D

const SPEED := 5.0
const JUMP_VELOCITY := 4.5
@export var mouse_sensitivity := 0.002

@onready var camera: Camera3D = $Camera3D

@onready var look_raycast: RayCast3D = $Camera3D/RayCast3D
var preview_marker: MeshInstance3D = null

# Get the gravity from the project settings to be synced with RigidBody nodes.
var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity")

var is_gravity_on := true
var pressed_jump := false
var input_dir := Vector2()

func _ready() -> void:
	if not preview_marker:
		preview_marker = MeshInstance3D.new()
		var sphere_mesh := SphereMesh.new()
		sphere_mesh.radius = 0.1
		sphere_mesh.height = 0.2
		preview_marker.mesh = sphere_mesh
		add_child(preview_marker)
		preview_marker.top_level = true

func _process(_dt: float) -> void:
	if (not look_raycast):
		return

	var target_pos := Vector3.ZERO
	if look_raycast.is_colliding():
		target_pos = look_raycast.get_collision_point()
	else:
		target_pos = look_raycast.to_global(look_raycast.target_position)

	if preview_marker:
		preview_marker.position = target_pos

func _unhandled_input(event: InputEvent) -> void:
	if Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED and (event is InputEventMouseMotion):
		process_mouse_input(event as InputEventMouseMotion)

	if Input.is_action_just_pressed("toggle_gravity"):
		is_gravity_on = not is_gravity_on;
	
	input_dir = Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	
	if Input.is_action_just_pressed("move_up"):
		pressed_jump = false

	if event is InputEventMouseButton:
		process_mouse_button(event as InputEventMouseButton)

func process_mouse_button(mouse_button: InputEventMouseButton) -> void:
	match mouse_button.button_index:
		MOUSE_BUTTON_LEFT:
			modify_terrain()

func modify_terrain() -> void:
	if (not look_raycast):
		return

	var target_pos := Vector3.ZERO
	if look_raycast.is_colliding():
		target_pos = look_raycast.get_collision_point()
	else:
		target_pos = look_raycast.to_global(look_raycast.target_position)

	var chunk_loader := GlobalChunkLoader as ChunkLoader
	if (chunk_loader):
		chunk_loader.modify_terrain(target_pos, true)

func process_mouse_input(mouse_motion: InputEventMouseMotion) -> void:
	# Rotate the whole character body left/right (Y-axis)
	rotate_y(-mouse_motion.relative.x * mouse_sensitivity)

	# Rotate the camera up/down (X-axis)
	camera.rotate_x(-mouse_motion.relative.y * mouse_sensitivity)

	# Clamp vertical rotation so you can't flip upside down
	camera.rotation.x = clamp(camera.rotation.x, deg_to_rad(-60), deg_to_rad(60))

func _physics_process(delta: float) -> void:
	if is_gravity_on and not is_on_floor():
		velocity.y -= gravity * delta

	if pressed_jump and is_on_floor():
		pressed_jump = false
		velocity.y = JUMP_VELOCITY

	var direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()
	if direction:
		velocity.x = direction.x * SPEED
		velocity.z = direction.z * SPEED
	else:
		velocity.x = move_toward(velocity.x, 0, SPEED)
		velocity.z = move_toward(velocity.z, 0, SPEED)

	move_and_slide()
