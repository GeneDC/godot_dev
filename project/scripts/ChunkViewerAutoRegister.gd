extends ChunkViewer

func _ready() -> void:
	var chunk_loader := GlobalChunkLoader as ChunkLoader
	if (chunk_loader):
		chunk_loader.chunk_viewer = self
