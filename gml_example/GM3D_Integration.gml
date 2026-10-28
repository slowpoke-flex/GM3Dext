// ==============================================================================
// GM3D Integration Example (GML Script / Object Events)
// ==============================================================================

// --- 1. CREATE EVENT (Инициализация) ---
function gm3d_init() {
    global.GM3D_MAX_ENTITIES = 10000;
    
    // Размер: Заголовок (64 байта) + N сущностей по 64 байта
    var header_size = 64;
    var entity_size = 64;
    var total_size = header_size + (global.GM3D_MAX_ENTITIES * entity_size);

    // Создаем фиксированный буфер с выравниванием 1 байт
    global.gm3d_buffer = buffer_create(total_size, buffer_fixed, 1);
    var buff_ptr = buffer_get_address(global.gm3d_buffer);
    var hwnd = window_handle();

    // Заполняем дефолтный заголовок буфера
    buffer_seek(global.gm3d_buffer, buffer_seek_start, 0);
    buffer_write(global.gm3d_buffer, buffer_u32, $474D3344); // Magic: 'GM3D'
    buffer_write(global.gm3d_buffer, buffer_u32, 1);          // Version: 1
    buffer_write(global.gm3d_buffer, buffer_u32, 0);          // Entity count: 0
    buffer_write(global.gm3d_buffer, buffer_u32, global.GM3D_MAX_ENTITIES);

    // Камера (положение X, Y, Z, таргет X, Y, Z, FOV, Near, Far)
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);  // Cam X
    buffer_write(global.gm3d_buffer, buffer_f32, 10.0); // Cam Y
    buffer_write(global.gm3d_buffer, buffer_f32, -15.0);// Cam Z
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);  // Target X
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);  // Target Y
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);  // Target Z
    buffer_write(global.gm3d_buffer, buffer_f32, 60.0); // FOV
    buffer_write(global.gm3d_buffer, buffer_f32, 0.1);  // Near
    buffer_write(global.gm3d_buffer, buffer_f32, 1000.0);// Far
    buffer_write(global.gm3d_buffer, buffer_u32, 0);    // Flags
    // Reserved (8 байт)
    buffer_write(global.gm3d_buffer, buffer_u32, 0);
    buffer_write(global.gm3d_buffer, buffer_u32, 0);

    // Вызов нативной DLL
    var init_res = init_extension(buff_ptr, total_size, hwnd, window_get_width(), window_get_height());
    if (init_res < 0) {
        show_error("Ошибка инициализации GM3Dext: " + string(init_res), true);
    }
}

// --- 2. STEP EVENT (Каждый кадр) ---
function gm3d_step() {
    var buff = global.gm3d_buffer;
    var dt = delta_time / 1000000.0; // секунды

    // Пример записи сущностей:
    // Обновляем количество сущностей в заголовке (байт 8..11)
    var num_entities = instance_number(obj_cube_3d);
    buffer_poke(buff, 8, buffer_u32, num_entities);

    // Запись трансформаций объектов в буфер
    var header_size = 64;
    var entity_size = 64;
    var idx = 0;

    with (obj_cube_3d) {
        var offset = header_size + (idx * entity_size);
        buffer_seek(buff, buffer_seek_start, offset);

        // Position (X, Y, Z)
        buffer_write(buff, buffer_f32, x);
        buffer_write(buff, buffer_f32, y);
        buffer_write(buff, buffer_f32, z);

        // Quaternion (QX, QY, QZ, QW)
        buffer_write(buff, buffer_f32, qx);
        buffer_write(buff, buffer_f32, qy);
        buffer_write(buff, buffer_f32, qz);
        buffer_write(buff, buffer_f32, qw);

        // Scale (SX, SY, SZ)
        buffer_write(buff, buffer_f32, image_xscale);
        buffer_write(buff, buffer_f32, image_yscale);
        buffer_write(buff, buffer_f32, image_zscale);

        // IDs & Flags
        buffer_write(buff, buffer_u32, id);
        buffer_write(buff, buffer_u16, mesh_id);
        buffer_write(buff, buffer_u16, material_id);
        buffer_write(buff, buffer_u32, flags);
        buffer_write(buff, buffer_u32, physics_body_id);

        // Custom params
        buffer_write(buff, buffer_f32, 1.0);
        buffer_write(buff, buffer_f32, 0.0);

        idx++;
    }

    // Вызываем шаг физики и отрисовку в DLL
    update_framework(dt);

    // Считываем обновленные координаты динамических физических тел обратно в GameMaker:
    idx = 0;
    with (obj_cube_3d) {
        if (flags & 4) { // IS_RIGIDBODY
            var offset = header_size + (idx * entity_size);
            x  = buffer_peek(buff, offset + 0,  buffer_f32);
            y  = buffer_peek(buff, offset + 4,  buffer_f32);
            z  = buffer_peek(buff, offset + 8,  buffer_f32);
            qx = buffer_peek(buff, offset + 12, buffer_f32);
            qy = buffer_peek(buff, offset + 16, buffer_f32);
            qz = buffer_peek(buff, offset + 20, buffer_f32);
            qw = buffer_peek(buff, offset + 24, buffer_f32);
        }
        idx++;
    }
}

// --- 3. CLEAN UP EVENT (Завершение работы) ---
function gm3d_cleanup() {
    shutdown_extension();
    if (buffer_exists(global.gm3d_buffer)) {
        buffer_delete(global.gm3d_buffer);
    }
}
