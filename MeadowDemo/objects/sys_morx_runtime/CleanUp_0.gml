
if (variable_instance_exists(id,"mcp_previous_error_handler")) {
    try { exception_unhandled_handler(mcp_previous_error_handler); } catch (err) {}
}
if (variable_instance_exists(id,"mcp_release")) mcp_release();
