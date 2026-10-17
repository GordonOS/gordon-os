// ==========================================
//  GordonOS - Task table
// ==========================================

// --- Task lookup tables (MAX_TASKS slots, populated at runtime) ---
//  The canonical taskLo/Hi tables, at $186F.
taskLo:
    .fill MAX_TASKS, 0
taskHi:
    .fill MAX_TASKS, 0

// (Every task loads from the REU FS via kDynLoad and the "run" command -- there
//  is no compile-time task registry.)
