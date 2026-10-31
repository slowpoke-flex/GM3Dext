
if (!is_undefined(mcp_capture)) {
    try {
        screen_save(mcp_directory+"capture_"+mcp_capture+".png");
        mcp_reply(mcp_capture,true,{frame:mcp_frame});
    } catch (err) {
        mcp_record_error("capture",err.message,err.longMessage,err.script,err.line,err.stacktrace);
        mcp_reply(mcp_capture,false,{error:"Capture failed: "+mcp_text(err.message,512)});
    }
    mcp_capture = undefined;
}
