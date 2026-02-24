#pragma once

#include <godot_cpp/classes/ref_counted.hpp>

/* When inheriting:
1. Use:			GDCLASS(MyTaskProcessor, RefCounted)
2. Use:			final (to de-virtualize the process_task for better performance)
3. Define:		virtual ~MyTaskProcessor() = default; (no override)
4. Define:		static void _bind_methods() {} (protected)
5. Construct:	memnew((MyTaskProcessor)) with double parentheses for templates (Don't use RefCounted::instantiate)
6. Register:	GDREGISTER_CLASS(MyTaskProcessor)

* Example MyTaskProcessor:

class MyTaskProcessor final : public ITaskProcessor<MyTask, MyResult>
{
	GDCLASS(MyTaskProcessor, godot::RefCounted);
public:
	virtual ~MyTaskProcessor() = default;
	virtual TResult process_task(MyTask task) override;
protected:
	static void _bind_methods() {}
};

*/

template <typename TTask, typename TResult>
class ITaskProcessor : public godot::RefCounted
{
public:
	virtual ~ITaskProcessor() = default;
	virtual TResult process_task(TTask task) = 0;
};
