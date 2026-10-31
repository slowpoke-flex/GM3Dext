// MORX_RUNTIME_BRIDGE 1 a02e5db2ce8844b685e4dec050930d52

mcp_directory = "morx_mcp/";
mcp_session = undefined;
mcp_read_json = function(_name) {
    var _path = mcp_directory + _name;
    if (!file_exists(_path)) return undefined;
    var _h = file_text_open_read(_path);
    var _raw = "";
    while (!file_text_eof(_h)) {
        _raw += file_text_read_string(_h);
        if (!file_text_eof(_h)) file_text_readln(_h);
    }
    file_text_close(_h);
    return json_parse(_raw);
};
try { mcp_session = mcp_read_json("session.json"); } catch (err) { instance_destroy(); exit; }
if (is_undefined(mcp_session)) { instance_destroy(); exit; }
if (!is_struct(mcp_session)) { instance_destroy(); exit; }
if (!variable_struct_exists(mcp_session,"bridge") || !variable_struct_exists(mcp_session,"session")) {
    instance_destroy(); exit;
}
if (mcp_session.bridge != "a02e5db2ce8844b685e4dec050930d52") { instance_destroy(); exit; }
persistent = true;
depth = -1000000000;
mcp_frame = 0;
mcp_last_request = "";
mcp_pending = undefined;
mcp_capture = undefined;
mcp_keys = [];
mcp_until = 0;
mcp_started = current_time;
mcp_write_pending = [];
mcp_write_failures = 0;
mcp_write_stats = {};
mcp_io_snapshot = function() {
    var files = [];
    var retry_total = 0;
    var recovery_total = 0;
    var stalled_total = 0;
    var known_names = ["ready.json","response.json","errors.json"];
    for (var di=0;di<array_length(known_names);di++) {
        var diagnostic_name = known_names[di];
        if (!variable_struct_exists(mcp_write_stats,diagnostic_name)) continue;
        var diagnostic = variable_struct_get(mcp_write_stats,diagnostic_name);
        var pending_age = diagnostic.pending_since_ms < 0 ? 0 : max(0,current_time-diagnostic.pending_since_ms);
        var pending_now = array_contains(mcp_write_pending,diagnostic_name);
        var state_name = pending_now ? (pending_age>=10000 ? "stalled" : "retrying")
            : (diagnostic.recovery_count>0 ? "recovered" : "healthy");
        if (pending_now && pending_age>=10000) stalled_total++;
        retry_total += diagnostic.retry_attempts;
        recovery_total += diagnostic.recovery_count;
        array_push(files,{file:diagnostic_name,state:state_name,pending:pending_now,
            pending_age_ms:pending_age,retry_attempts:diagnostic.retry_attempts,
            current_retry_attempts:diagnostic.current_retry_attempts,recovery_count:diagnostic.recovery_count,
            last_recovery_attempts:diagnostic.last_recovery_attempts,
            first_failure_frame:diagnostic.first_failure_frame,last_failure_frame:diagnostic.last_failure_frame,
            last_recovery_frame:diagnostic.last_recovery_frame,last_failure_stage:diagnostic.last_failure_stage,
            request:diagnostic.request,last_failure_request:diagnostic.last_failure_request,
            last_recovery_request:diagnostic.last_recovery_request});
    }
    return {state:stalled_total>0 ? "stalled" : (array_length(mcp_write_pending)>0 ? "retrying"
            : (retry_total>0 ? "recovered" : "healthy")),
        pending_writes:array_length(mcp_write_pending),stalled_writes:stalled_total,
        retry_attempts:retry_total,retry_recoveries:recovery_total,
        write_failures:retry_total,write_failures_semantics:"retry_attempts_not_final_failures",
        sample_frame:mcp_frame,stall_threshold_ms:10000,files:files};
};
mcp_last_mouse_press = undefined;
mcp_last_mouse_release = undefined;
mcp_mouse_trace = undefined;
mcp_mouse_sample = function(_button) {
    return {frame:mcp_frame,window_x:window_mouse_get_x(),window_y:window_mouse_get_y(),
        gui_x:device_mouse_x_to_gui(0),gui_y:device_mouse_y_to_gui(0),
        down:mouse_check_button(_button),pressed:mouse_check_button_pressed(_button),
        released:mouse_check_button_released(_button)};
};
mcp_sample_mouse_trace = function() {
    if (is_undefined(mcp_mouse_trace) || !mcp_mouse_trace.active) return;
    var trace_sample = mcp_mouse_sample(mcp_mouse_trace.button_code);
    trace_sample.elapsed_ms = max(0,current_time-mcp_mouse_trace.started_ms);
    if (array_length(mcp_mouse_trace.samples)>=128) {
        // 保留首帧及最近 127 帧；按下/释放另存，不因截断丢失。
        array_delete(mcp_mouse_trace.samples,1,1);
        mcp_mouse_trace.dropped_samples++;
    }
    array_push(mcp_mouse_trace.samples,trace_sample);
    if (trace_sample.pressed) mcp_mouse_trace.press = trace_sample;
    if (trace_sample.released) mcp_mouse_trace.release = trace_sample;
    if (trace_sample.elapsed_ms>=10000) {
        mcp_mouse_trace.active = false;
        mcp_mouse_trace.expired = true;
        mcp_mouse_trace.end_frame = mcp_frame;
    }
};
mcp_flush_writes = function() {
    var remaining = [];
    for (var wi=0;wi<array_length(mcp_write_pending);wi++) {
        var write_name = mcp_write_pending[wi];
        var write_temp = mcp_directory+write_name+".tmp";
        var write_dest = mcp_directory+write_name;
        var write_stat = variable_struct_get(mcp_write_stats,write_name);
        if (!file_exists(write_temp)) {
            // 保留诊断，不把临时文件缺失误报为已经恢复；不重新执行原命令。
            write_stat.last_failure_stage = "temporary_file_missing";
            array_push(remaining,write_name);
            continue;
        }
        if (file_exists(write_dest)) file_delete(write_dest);
        if (!file_rename(write_temp,write_dest)) {
            array_push(remaining,write_name);
            mcp_write_failures++;
            write_stat.retry_attempts++;
            write_stat.current_retry_attempts++;
            if (write_stat.first_failure_frame<0) write_stat.first_failure_frame=mcp_frame;
            write_stat.last_failure_frame=mcp_frame;
            write_stat.last_failure_request=write_stat.request;
            write_stat.last_failure_stage=file_exists(write_dest) ? "replace_destination" : "promote_temporary";
        } else {
            if (write_stat.current_retry_attempts>0) {
                write_stat.recovery_count++;
                write_stat.last_recovery_attempts=write_stat.current_retry_attempts;
                write_stat.last_recovery_frame=mcp_frame;
                write_stat.last_recovery_request=write_stat.request;
            }
            write_stat.current_retry_attempts=0;
            write_stat.pending_since_ms=-1;
        }
    }
    mcp_write_pending = remaining;
};
mcp_write = function(_name,_value) {
    if (!variable_struct_exists(mcp_write_stats,_name)) {
        variable_struct_set(mcp_write_stats,_name,{retry_attempts:0,current_retry_attempts:0,recovery_count:0,
            last_recovery_attempts:0,pending_since_ms:-1,first_failure_frame:-1,last_failure_frame:-1,
            last_recovery_frame:-1,last_failure_stage:"",request:"",last_failure_request:"",last_recovery_request:""});
    }
    var write_stat = variable_struct_get(mcp_write_stats,_name);
    if (!array_contains(mcp_write_pending,_name)) write_stat.pending_since_ms=current_time;
    if (_name=="response.json" && variable_struct_exists(_value,"request")) {
        write_stat.request=string_copy(string(_value.request),1,32);
    }
    var temp_name = mcp_directory + _name + ".tmp";
    var dest_name = mcp_directory + _name;
    var h = file_text_open_write(temp_name);
    file_text_write_string(h,json_stringify(_value)); file_text_close(h);
    if (!array_contains(mcp_write_pending,_name)) array_push(mcp_write_pending,_name);
    mcp_flush_writes();
};
mcp_reply = function(_request,_ok,_data) {
    mcp_write("response.json",{session:mcp_session.session,request:_request,
        ok:_ok,data:_data,frame:mcp_frame});
};
mcp_release = function() {
    for (var k=0;k<array_length(mcp_keys);k++) keyboard_key_release(mcp_keys[k]);
    mcp_keys = [];
};
mcp_value = function(_value,_depth,_budget) {
    var depth_limit = variable_struct_exists(_budget,"depth") ? _budget.depth : 3;
    if (_budget.left <= 0) return {type:"truncated"};
    _budget.left -= 1;
    if (is_undefined(_value)) return {type:"undefined"};
    if (is_bool(_value)) return _value;
    if (is_int64(_value)) {
        if (_value > int64(9007199254740991) || _value < int64(-9007199254740991))
            return {type:"int64",value:string(_value)};
        return real(_value);
    }
    if (is_real(_value)) {
        if (is_nan(_value) || is_infinity(_value)) return {type:"non_finite"};
        return _value;
    }
    if (is_string(_value)) return string_copy(_value,1,1024);
    if (is_array(_value)) {
        var array_length_value = array_length(_value);
        if (_depth >= depth_limit) return {type:"array",length:array_length_value,items:[],truncated:true};
        var items = [];
        var array_take = min(array_length_value,32);
        for (var ai=0;ai<array_take;ai++) {
            array_push(items,mcp_value(_value[ai],_depth+1,_budget));
            if (_budget.left <= 0) break;
        }
        return {type:"array",length:array_length_value,items:items,
            truncated:(array_length(items)<array_length_value || _budget.left<=0)};
    }
    if (is_struct(_value)) {
        var names = variable_struct_get_names(_value);
        var struct_length_value = array_length(names);
        if (_depth >= depth_limit) return {type:"struct",length:struct_length_value,values:{},truncated:true};
        var values = {};
        var struct_take = min(struct_length_value,32);
        var struct_count = 0;
        for (var si=0;si<struct_take;si++) {
            var struct_field = names[si];
            variable_struct_set(values,struct_field,
                mcp_value(variable_struct_get(_value,struct_field),_depth+1,_budget));
            struct_count += 1;
            if (_budget.left <= 0) break;
        }
        return {type:"struct",length:struct_length_value,values:values,
            truncated:(struct_count<struct_length_value || _budget.left<=0)};
    }
    return {type:"complex"};
};
mcp_snapshot = function(_args) {
    var read_depth = variable_struct_exists(_args,"max_depth") ? clamp(_args.max_depth,1,8) : 3;
    var scope = "instances";
    if (variable_struct_exists(_args,"scope")) scope = _args.scope;
    var base = {room:room_get_name(room),room_width:room_width,room_height:room_height,
        window_width:window_get_width(),window_height:window_get_height(),
        gui_width:display_get_gui_width(),gui_height:display_get_gui_height(),
        frame:mcp_frame,fps:fps_real,scope:scope,
        mouse:{window_x:window_mouse_get_x(),window_y:window_mouse_get_y(),
            gui_x:device_mouse_x_to_gui(0),gui_y:device_mouse_y_to_gui(0),
            last_press:mcp_last_mouse_press,last_release:mcp_last_mouse_release,trace:mcp_mouse_trace},
        bridge_io:mcp_io_snapshot()};
    if (scope == "globals") {
        var globals = {};
        for (var gj=0;gj<array_length(_args.variables);gj++) {
            var global_field = _args.variables[gj];
            if (variable_global_exists(global_field)) {
                variable_struct_set(globals,global_field,
                    mcp_value(variable_global_get(global_field),0,{left:64,depth:read_depth}));
            } else variable_struct_set(globals,global_field,{type:"missing"});
        }
        base.globals = globals;
        base.truncated = false;
        return base;
    }
    var rows = [];
    var source = all;
    if (_args.object_name != "") {
        source = asset_get_index(_args.object_name);
        if (source == -1) return {error:"Object not found in running build"};
    }
    var single = variable_struct_exists(_args,"instance_id") && is_real(_args.instance_id);
    if (single) {
        var target_instance = _args.instance_id;
        if (!instance_exists(target_instance)) return {error:"Instance not found in running build"};
        if (_args.object_name != "" && target_instance.object_index != source) {
            return {error:"Instance does not match object_name"};
        }
        var one_values = {};
        for (var sj=0;sj<array_length(_args.variables);sj++) {
            var one_field = _args.variables[sj];
            if (variable_instance_exists(target_instance,one_field)) {
                variable_struct_set(one_values,one_field,
                    mcp_value(variable_instance_get(target_instance,one_field),0,{left:64,depth:read_depth}));
            } else variable_struct_set(one_values,one_field,{type:"missing"});
        }
        base.instances = [{id:target_instance.id,
            object_name:object_get_name(target_instance.object_index),values:one_values}];
        base.truncated = false;
        return base;
    }
    var total = instance_number(source);
    var visible_total = total;
    if (_args.object_name == "") visible_total -= 1;
    for (var i=0;i<total && array_length(rows)<_args.limit;i++) {
        var inst = instance_find(source,i);
        if (inst == id) continue;
        var values = {};
        for (var j=0;j<array_length(_args.variables);j++) {
            var field = _args.variables[j];
            if (variable_instance_exists(inst,field)) {
                variable_struct_set(values,field,
                    mcp_value(variable_instance_get(inst,field),0,{left:64,depth:read_depth}));
            } else variable_struct_set(values,field,{type:"missing"});
        }
        array_push(rows,{id:inst.id,object_name:object_get_name(inst.object_index),values:values});
    }
    base.instances = rows;
    base.truncated = visible_total>array_length(rows);
    return base;
};
mcp_collect_targets = function(_args,_source) {
    var targets = [];
    if (variable_struct_exists(_args,"instance_id")) {
        var one = _args.instance_id;
        if (!instance_exists(one)) return {error:"Instance not found in running build"};
        if (_source != all && one.object_index != _source) {
            return {error:"Instance does not match object_name"};
        }
        array_push(targets,one);
        return {targets:targets};
    }
    if (_source == all) return {error:"object_name or instance_id required"};
    var total = instance_number(_source);
    var cap = min(total,_args.limit);
    for (var i=0;i<cap;i++) array_push(targets,instance_find(_source,i));
    return {targets:targets};
};
mcp_bridge_asset = asset_get_index("sys_morx_runtime");
mcp_errors = [];
mcp_text = function(_value,_limit) {
    if (is_undefined(_value)) return "";
    return string_copy(string(_value),1,_limit);
};
mcp_record_error = function(_stage,_message,_long,_script,_line,_stack) {
    var item = {frame:mcp_frame,stage:_stage,message:mcp_text(_message,2048),
        long_message:mcp_text(_long,4096),script:mcp_text(_script,1024),
        line:is_real(_line)?_line:-1,stacktrace:[]};
    if (is_array(_stack)) {
        for (var i=0;i<array_length(_stack) && i<32;i++) {
            array_push(item.stacktrace,mcp_text(_stack[i],1024));
        }
    }
    array_push(mcp_errors,item);
    if (array_length(mcp_errors)>20) array_delete(mcp_errors,0,array_length(mcp_errors)-20);
    try { mcp_write("errors.json",{session:mcp_session.session,errors:mcp_errors}); } catch (err) {}
};
mcp_previous_error_handler = undefined;
try {
    mcp_previous_error_handler = exception_unhandled_handler(function(_exception) {
        mcp_record_error("unhandled",_exception.message,_exception.longMessage,
            _exception.script,_exception.line,_exception.stacktrace);
    });
} catch (err) {}
mcp_write("ready.json",{session:mcp_session.session,bridge:mcp_session.bridge,frame:0,bridge_io:mcp_io_snapshot()});
