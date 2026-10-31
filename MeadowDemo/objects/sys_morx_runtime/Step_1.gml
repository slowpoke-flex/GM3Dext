
mcp_frame++;
mcp_flush_writes();
if (mouse_check_button_pressed(mb_left) || mouse_check_button_pressed(mb_right)
    || mouse_check_button_pressed(mb_middle)) {
    mcp_last_mouse_press = {frame:mcp_frame,window_x:window_mouse_get_x(),window_y:window_mouse_get_y(),
        gui_x:device_mouse_x_to_gui(0),gui_y:device_mouse_y_to_gui(0)};
}
if (mouse_check_button_released(mb_left) || mouse_check_button_released(mb_right)
    || mouse_check_button_released(mb_middle)) {
    mcp_last_mouse_release = {frame:mcp_frame,window_x:window_mouse_get_x(),window_y:window_mouse_get_y(),
        gui_x:device_mouse_x_to_gui(0),gui_y:device_mouse_y_to_gui(0)};
}
mcp_sample_mouse_trace();
if (current_time-mcp_started > 900000 || ((mcp_frame mod 5) == 0 && !file_exists(mcp_directory+"session.json"))) {
    mcp_release(); game_end(); exit;
}
if (array_length(mcp_keys)>0 && !window_has_focus()) {
    mcp_release();
    if (!is_undefined(mcp_pending)) mcp_reply(mcp_pending.request,false,{error:"Game lost focus; input released"});
    mcp_pending = undefined;
}
if (!is_undefined(mcp_pending) && mcp_frame>=mcp_until) {
    mcp_release();
    mcp_reply(mcp_pending.request,true,{frames:mcp_pending.frames});
    mcp_pending = undefined;
}
if ((mcp_frame mod 60) == 0) {
    mcp_write("ready.json",{session:mcp_session.session,bridge:mcp_session.bridge,frame:mcp_frame,bridge_io:mcp_io_snapshot()});
}
if (!file_exists(mcp_directory+"request.json")) exit;
var request;
try { request = mcp_read_json("request.json"); } catch (err) { exit; }
if (is_undefined(request) || !is_struct(request)) exit;
if (!variable_struct_exists(request,"session") || !variable_struct_exists(request,"request")) exit;
if (request.session != mcp_session.session || request.request == mcp_last_request) exit;
mcp_last_request = request.request;
file_delete(mcp_directory+"request.json");
try {
    if (!is_undefined(mcp_pending) || !is_undefined(mcp_capture)) {
        mcp_reply(request.request,false,{error:"Another command is pending"}); exit;
    }
    switch (request.command) {
        case "mouse_trace":
            if (request.args.phase == "begin") {
                var trace_button = request.args.button;
                var trace_button_code = trace_button == "right" ? mb_right
                    : (trace_button == "middle" ? mb_middle : mb_left);
                mcp_mouse_trace = {trace_id:request.args.trace_id,source:"BeginStep",
                    button:trace_button,button_code:trace_button_code,active:true,expired:false,
                    start_frame:mcp_frame,end_frame:mcp_frame,started_ms:current_time,
                    capacity:128,dropped_samples:0,samples:[],press:undefined,release:undefined};
                mcp_sample_mouse_trace();
            } else if (request.args.phase == "end" && !is_undefined(mcp_mouse_trace)
                && mcp_mouse_trace.trace_id == request.args.trace_id) {
                mcp_mouse_trace.active = false;
                mcp_mouse_trace.end_frame = mcp_frame;
            } else {
                mcp_reply(request.request,false,{error:"Mouse trace does not match"}); break;
            }
            mcp_reply(request.request,true,mcp_mouse_trace);
            break;
        case "state":
            mcp_reply(request.request,true,mcp_snapshot(request.args));
            break;
        case "set":
            var set_scope = request.args.scope;
            var set_values = request.args.values;
            if (!is_struct(set_values)) {
                mcp_reply(request.request,false,{error:"values must be a struct"}); break;
            }
            var set_names = variable_struct_get_names(set_values);
            if (array_length(set_names)<1 || array_length(set_names)>16) {
                mcp_reply(request.request,false,{error:"values must contain 1 to 16 variables"}); break;
            }
            var set_create = false;
            if (variable_struct_exists(request.args,"create_missing")) set_create = request.args.create_missing;
            if (set_scope == "globals") {
                var set_global_error = "";
                for (var sg=0;sg<array_length(set_names);sg++) {
                    var set_gname = set_names[sg];
                    if (set_gname == "id" || set_gname == "object_index") {
                        set_global_error = "Protected variable: "+set_gname;
                    } else if (!variable_global_exists(set_gname) && !set_create) {
                        set_global_error = "Global variable not found: "+set_gname;
                    }
                    if (set_global_error != "") break;
                }
                if (set_global_error != "") {
                    mcp_reply(request.request,false,{error:set_global_error}); break;
                }
                var set_global_previous = {};
                var set_global_created = [];
                for (var sw=0;sw<array_length(set_names);sw++) {
                    var set_w_name = set_names[sw];
                    if (variable_global_exists(set_w_name)) {
                        variable_struct_set(set_global_previous,set_w_name,
                            mcp_value(variable_global_get(set_w_name),0,{left:64}));
                    } else {
                        variable_struct_set(set_global_previous,set_w_name,{type:"missing"});
                        array_push(set_global_created,set_w_name);
                    }
                    variable_global_set(set_w_name,
                        variable_clone(variable_struct_get(set_values,set_w_name)));
                }
                mcp_reply(request.request,true,{scope:"globals",previous:set_global_previous,
                    created:set_global_created,count:array_length(set_names),frame:mcp_frame});
                break;
            }
            if (set_scope != "instances") {
                mcp_reply(request.request,false,{error:"Invalid scope"}); break;
            }
            var set_source = all;
            if (request.args.object_name != "") {
                set_source = asset_get_index(request.args.object_name);
                if (set_source == -1) {
                    mcp_reply(request.request,false,{error:"Object not found in running build"}); break;
                }
                if (set_source == mcp_bridge_asset) {
                    mcp_reply(request.request,false,{error:"Managed bridge cannot be modified"}); break;
                }
            }
            var set_collected = mcp_collect_targets(request.args,set_source);
            if (variable_struct_exists(set_collected,"error")) {
                mcp_reply(request.request,false,{error:set_collected.error}); break;
            }
            var set_targets = set_collected.targets;
            if (array_length(set_targets)<1) {
                mcp_reply(request.request,false,{error:"No matching instances"}); break;
            }
            var set_target_error = "";
            for (var st=0;st<array_length(set_targets);st++) {
                var set_target = set_targets[st];
                if (set_target == id || set_target.object_index == mcp_bridge_asset) {
                    set_target_error = "Managed bridge cannot be modified";
                    break;
                }
                for (var sn=0;sn<array_length(set_names);sn++) {
                    var set_name = set_names[sn];
                    if (set_name == "id" || set_name == "object_index") {
                        set_target_error = "Protected variable: "+set_name;
                    } else if (!variable_instance_exists(set_target,set_name) && !set_create) {
                        set_target_error = "Instance variable not found: "+set_name;
                    }
                    if (set_target_error != "") break;
                }
                if (set_target_error != "") break;
            }
            if (set_target_error != "") {
                mcp_reply(request.request,false,{error:set_target_error}); break;
            }
            var set_rows = [];
            for (var sr=0;sr<array_length(set_targets);sr++) {
                var set_row_target = set_targets[sr];
                var set_previous = {};
                for (var sv=0;sv<array_length(set_names);sv++) {
                    var set_v_name = set_names[sv];
                    if (variable_instance_exists(set_row_target,set_v_name)) {
                        variable_struct_set(set_previous,set_v_name,
                            mcp_value(variable_instance_get(set_row_target,set_v_name),0,{left:64}));
                    } else variable_struct_set(set_previous,set_v_name,{type:"missing"});
                    variable_instance_set(set_row_target,set_v_name,
                        variable_clone(variable_struct_get(set_values,set_v_name)));
                }
                array_push(set_rows,{id:set_row_target.id,
                    object_name:object_get_name(set_row_target.object_index),previous:set_previous});
            }
            mcp_reply(request.request,true,{scope:"instances",instances:set_rows,
                count:array_length(set_names),frame:mcp_frame});
            break;
        case "spawn":
            var spawn_source = asset_get_index(request.args.object_name);
            if (spawn_source == -1) {
                mcp_reply(request.request,false,{error:"Object not found in running build"}); break;
            }
            if (spawn_source == mcp_bridge_asset) {
                mcp_reply(request.request,false,{error:"Managed bridge cannot be spawned"}); break;
            }
            var spawn_instance = noone;
            if (request.args.layer_name != "") {
                if (!layer_exists(request.args.layer_name)) {
                    mcp_reply(request.request,false,{error:"Layer not found in running room"}); break;
                }
                spawn_instance = instance_create_layer(request.args.x,request.args.y,
                    request.args.layer_name,spawn_source);
            } else {
                spawn_instance = instance_create_depth(request.args.x,request.args.y,
                    request.args.depth,spawn_source);
            }
            if (!instance_exists(spawn_instance)) {
                mcp_reply(request.request,false,{error:"Instance creation failed"}); break;
            }
            mcp_reply(request.request,true,{instance:{id:spawn_instance.id,
                object_name:object_get_name(spawn_instance.object_index),
                x:spawn_instance.x,y:spawn_instance.y},frame:mcp_frame});
            break;
        case "destroy":
            var destroy_source = all;
            if (request.args.object_name != "") {
                destroy_source = asset_get_index(request.args.object_name);
                if (destroy_source == -1) {
                    mcp_reply(request.request,false,{error:"Object not found in running build"}); break;
                }
                if (destroy_source == mcp_bridge_asset) {
                    mcp_reply(request.request,false,{error:"Managed bridge cannot be destroyed"}); break;
                }
            }
            var destroy_collected = mcp_collect_targets(request.args,destroy_source);
            if (variable_struct_exists(destroy_collected,"error")) {
                mcp_reply(request.request,false,{error:destroy_collected.error}); break;
            }
            var destroy_targets = destroy_collected.targets;
            var destroy_rows = [];
            var destroy_skipped = 0;
            for (var dt=0;dt<array_length(destroy_targets);dt++) {
                var destroy_target = destroy_targets[dt];
                if (destroy_target == id || !instance_exists(destroy_target)
                    || destroy_target.object_index == mcp_bridge_asset) {
                    destroy_skipped++;
                    continue;
                }
                array_push(destroy_rows,{id:destroy_target.id,
                    object_name:object_get_name(destroy_target.object_index)});
                instance_destroy(destroy_target);
            }
            mcp_reply(request.request,true,{destroyed:destroy_rows,count:array_length(destroy_rows),
                skipped:destroy_skipped,frame:mcp_frame});
            break;
        case "capture":
            mcp_capture = request.request;
            break;
        case "wait":
        case "input":
            if (request.args.frames<1 || request.args.frames>600) {
                mcp_reply(request.request,false,{error:"Invalid frame count"}); break;
            }
            if (request.command == "input") {
                if (!window_has_focus()) {
                    mcp_reply(request.request,false,{error:"Game must have focus for keyboard input"}); break;
                }
                var allowed = [8,9,13,27,32,33,34,35,36,37,38,39,40,45,46];
                var valid = true;
                if (array_length(request.args.keys)>8) valid = false;
                for (var i=0;i<array_length(request.args.keys);i++) {
                    var key = request.args.keys[i];
                    if (!((key>=48 && key<=57) || (key>=65 && key<=90)
                        || array_contains(allowed,key))) valid = false;
                }
                if (!valid) { mcp_reply(request.request,false,{error:"Unsupported key"}); break; }
                mcp_keys = request.args.keys;
                for (var i=0;i<array_length(mcp_keys);i++) keyboard_key_press(mcp_keys[i]);
            }
            mcp_pending = {request:request.request,frames:request.args.frames};
            mcp_until = mcp_frame + request.args.frames;
            break;
        case "event":
            var ev_name = request.args.object_name;
            var ev_source = asset_get_index(ev_name);
            if (ev_source == -1) {
                mcp_reply(request.request,false,{error:"Object not found in running build"}); break;
            }
            var ev_type = request.args.event_type;
            var ev_number = request.args.event_number;
            if (!is_real(ev_type) || !is_real(ev_number)) {
                mcp_reply(request.request,false,{error:"Invalid event"}); break;
            }
            if (ev_type == 4) {
                if (!variable_struct_exists(request.args,"collision_object")) {
                    mcp_reply(request.request,false,{error:"Collision target required"}); break;
                }
                ev_number = asset_get_index(request.args.collision_object);
                if (ev_number == -1) {
                    mcp_reply(request.request,false,{error:"Collision object not found"}); break;
                }
            }
            var ev_targets = [];
            if (variable_struct_exists(request.args,"instance_id")) {
                var ev_instance = request.args.instance_id;
                if (!instance_exists(ev_instance)) {
                    mcp_reply(request.request,false,{error:"Instance not found"}); break;
                }
                if (ev_instance.object_index != ev_source) {
                    mcp_reply(request.request,false,{error:"Instance does not match object"}); break;
                }
                array_push(ev_targets,ev_instance);
            } else {
                var ev_total = instance_number(ev_source);
                if (ev_total<1) {
                    mcp_reply(request.request,false,{error:"No instances of object"}); break;
                }
                var ev_cap = min(ev_total,request.args.limit);
                for (var ev_i=0;ev_i<ev_cap;ev_i++) array_push(ev_targets,instance_find(ev_source,ev_i));
            }
            var ev_performed = 0;
            for (var ev_j=0;ev_j<array_length(ev_targets);ev_j++) {
                var ev_target = ev_targets[ev_j];
                if (!instance_exists(ev_target)) continue;
                with (ev_target) {
                    event_perform_object(ev_source,ev_type,ev_number);
                }
                ev_performed++;
            }
            mcp_reply(request.request,true,{object_name:ev_name,event:request.args.event,
                event_type:ev_type,event_number:ev_number,instances:ev_performed,frame:mcp_frame});
            break;
        default: mcp_reply(request.request,false,{error:"Unsupported command"});
    }
} catch (err) {
    mcp_release();
    mcp_record_error("bridge",err.message,err.longMessage,err.script,err.line,err.stacktrace);
    mcp_reply(request.request,false,{error:"Bridge command failed: "+mcp_text(err.message,512)});
}
