/*
The XSM machine simulator.
*/

#include "machine.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static xsm_cpu _thecpu;

static xsm_options _theoptions;

const char *instructions[] = {
    "MOV",
    "ADD",
    "SUB",
    "MUL",
    "DIV",
    "MOD",
    "INR",
    "DCR",
    "LT",
    "GT",
    "EQ",
    "NE",
    "GE",
    "LE",
    "JZ",
    "JNZ",
    "JMP",
    "PUSH",
    "POP",
    "CALL",
    "RET",
    "BRKP",
    "INT",

    "LOADI",
    "LOAD",
    "STORE",
    "ENCRYPT",
    "BACKUP",
    "RESTORE",
    "PORT",
    "IN",
    "INI",
    "OUT",
    "IRET",

    "START",
    "RESET",
    "TSL",

    "HALT",
    "NOP"};

/* Initialise the XSM machine */
int machine_init(xsm_options *options)
{
    xsm_word *ipreg, *core_flag;

    _theoptions = *options;

    if (!registers_init())
        return XSM_FAILURE;

    if (!memory_init())
        return XSM_FAILURE;

    if (!debug_init())
        return FALSE;

    /* Storing ROM code */
    word_store_string(memory_get_word(0), "LOADI 1, 0");
    word_store_string(memory_get_word(2), "JMP 512");

    /* Set up IP */
    ipreg = machine_get_ipreg(PRIMARY_CORE);
    word_store_integer(ipreg, 0);

    /* Set up CORE flag */
    core_flag = registers_get_register("CORE", PRIMARY_CORE);
    word_store_integer(core_flag, PRIMARY_CORE);
    core_flag = registers_get_register("CORE", SECONDARY_CORE);
    word_store_integer(core_flag, SECONDARY_CORE);

    /* Set up the primary core */
    machine_set_core(PRIMARY_CORE);

    /* Set the mode */
    machine_set_mode(PRIVILEGE_KERNEL);

    /* The disk and console is idle.*/
    _thecpu.console_state = XSM_CONSOLE_IDLE;
    _thecpu.disk_state = XSM_DISK_IDLE;

    /* Initialise timer clock*/
    _thecpu.timer[PRIMARY_CORE] = _theoptions.timer;

    /* Reset the core state */
    _thecpu.core_state = RESET_MODE;

    return XSM_SUCCESS;
}

/* Initialise the secondary core */
int machine_init_dual()
{
    xsm_word *ipreg, *core_flag;

    /* Set up IP */
    ipreg = registers_get_register("IP", SECONDARY_CORE);
    word_store_integer(ipreg, DUAL_BOOTSTRAP);

    /* Set the mode */
    _thecpu.mode[SECONDARY_CORE] = PRIVILEGE_KERNEL;

    /* Initialise timer clock*/
    _thecpu.timer[SECONDARY_CORE] = _theoptions.timer;

    /* Reset the mode for secondary core */
    machine_set_core_state(ACTIVE_MODE);

    return XSM_SUCCESS;
}

/* Retrieve the opcode */
int machine_get_opcode(const char *instr)
{
    int i;

    for (i = 0; i < XSM_INSTRUCTION_COUNT; ++i)
        if (!strcasecmp(instr, instructions[i]))
            return i;

    return XSM_ILLINSTR;
}

/* Retieve the IP register */
xsm_word *machine_get_ipreg()
{
    int core = machine_get_core();
    return registers_get_register("IP", core);
}

/* Retrieve the SP register */
xsm_word *machine_get_spreg()
{
    int core = machine_get_core();
    return registers_get_register("SP", core);
}

/* Retrieve the given register */
xsm_word *machine_get_register(const char *name)
{
    int mode, core;
    xsm_word *reg;

    core = machine_get_core();
    mode = machine_get_mode();
    reg = registers_get_register(name, core);

    if (!reg)
        machine_register_exception("No such register", EXP_ILLINSTR);

    if (mode == PRIVILEGE_USER)
        if (!registers_umode(name))
            machine_register_exception("Register not available in USER mode", EXP_ILLINSTR);

    if (!strcasecmp(name, "IP"))
        machine_register_exception("IP register can not be directly manipulated", EXP_ILLINSTR);

    if (!strcasecmp(name, "CORE"))
        machine_register_exception("CORE flag can not be manipulated", EXP_ILLINSTR);

    return reg;
}

/* Retrieve the given read-only register */
xsm_word *machine_read_register(const char *name)
{
    int mode, core;
    xsm_word *reg;

    core = machine_get_core();
    mode = machine_get_mode();
    reg = registers_get_register(name, core);

    if (!reg)
        machine_register_exception("No such register", EXP_ILLINSTR);

    if (mode == PRIVILEGE_USER)
        if (!registers_umode(name))
            machine_register_exception("Register not available in USER mode", EXP_ILLINSTR);

    if (!strcasecmp(name, "IP"))
        machine_register_exception("IP register can not be directly manipulated", EXP_ILLINSTR);

    return reg;
}

/* Returns the opcode privilege */
int machine_instr_req_privilege(int opcode)
{
    if (opcode >= TOKEN_KERN_LOW && opcode <= TOKEN_KERN_HIGH)
        return PRIVILEGE_KERNEL;

    return PRIVILEGE_USER;
}

/* An interface between lexer and machine */
int machine_serve_instruction(char *buffer, unsigned long *read_bytes, int max)
{

    int ip_val, i, j, bytes_to_read, core;
    xsm_word *ip_reg, *instr_mem;

    bytes_to_read = XSM_INSTRUCTION_SIZE * XSM_WORD_SIZE;
    core = machine_get_core();

    ip_reg = machine_get_ipreg();
    ip_val = word_get_integer(ip_reg);
    ip_val = machine_translate_address(ip_val, FALSE, INSTR_FETCH, machine_get_core(), machine_get_mode());
    instr_mem = machine_memory_get_word(ip_val);

    memcpy(buffer, instr_mem->val, bytes_to_read);

    if (strlen(buffer) == 0)
    {
        word_store_integer(machine_get_ipreg(), ip_val + XSM_INSTRUCTION_SIZE);
        machine_register_exception("The simulator has encountered a NULL instruction", EXP_ILLINSTR);
    }

    /* Trim */
    for (i = 0; i < bytes_to_read; i++)
        if (buffer[i] == '\0')
            for (j = i; j < bytes_to_read / 2; j++)
                buffer[j] = ' ';

    buffer[bytes_to_read - 1] = '\0';
    *read_bytes = bytes_to_read;

    return TRUE;
}

/* Start the XSM machine */
int machine_run()
{
    int token, opcode, ipval, exp_occured, core;
    YYSTYPE token_info;
    xsm_word *ipreg;

    while (TRUE)
    {
        /* Set the exception point */
        exp_occured = setjmp(_thecpu.h_exp_point);

        if (exp_occured == XSM_EXCEPTION_OCCURED)
        {
            if (machine_handle_exception() != XSM_SUCCESS)
                break;
            machine_change_core();
        }

        /* Flush the instruction stream */
        tokenize_clear_stream();
        tokenize_reset();

        /* Pre-execute */
        ipreg = machine_get_ipreg();
        ipval = word_get_integer(ipreg);
        machine_pre_execute(ipval);

        token = tokenize_next_token(&token_info);

        /* IP = IP + instruction length */
        ipval = ipval + XSM_INSTRUCTION_SIZE;
        word_store_integer(ipreg, ipval);

        if (token != TOKEN_INSTRUCTION)
            machine_register_exception("The simulator has encountered an illegal instruction", EXP_ILLINSTR);

        opcode = machine_get_opcode(token_info.str);

        if (opcode == XSM_ILLINSTR)
            machine_register_exception("The instruction is not available in this architecture", EXP_ILLINSTR);

        if (machine_instr_req_privilege(opcode) == PRIVILEGE_KERNEL && machine_get_mode() == PRIVILEGE_USER)
            machine_register_exception("This instruction requires more privilege", EXP_ILLINSTR);

        /* Stop the machine */
        if (machine_execute_instruction(opcode) == XSM_HALT)
            break;

        /* Post-execute */
        if (machine_get_mode() == PRIVILEGE_USER)
            machine_post_execute();

        machine_change_core();
    }

    return TRUE;
}

/* Changes the core in the machine */
void machine_change_core()
{
    if (machine_get_core_state() == ACTIVE_MODE)
    {
        if (machine_get_core() == PRIMARY_CORE)
            machine_set_core(SECONDARY_CORE);
        else
            machine_set_core(PRIMARY_CORE);
    }
}

/* Set the exception values */
void machine_register_exception(char *message, int code)
{
    int mode = machine_get_mode();
    exception_set(message, code, mode);

    /* Abandon ship! Abandon ship! */
    longjmp(_thecpu.h_exp_point, XSM_EXCEPTION_OCCURED);
}

/* Handle the exception */
int machine_handle_exception()
{
    int code, mode, core, curr_ip, num_regs, i;
    char *message, *content;
    const char **reg_names;
    xsm_word *reg_eip, *reg_epn, *reg_ec, *reg_ema;

    curr_ip = word_get_integer(machine_get_ipreg());
    curr_ip = curr_ip - XSM_INSTRUCTION_SIZE;
    word_store_integer(machine_get_ipreg(), curr_ip);

    /* Get the details about the exception. */
    core = machine_get_core();
    mode = machine_get_mode();
    code = exception_code();
    message = exception_message();

    /* Get the exception registers. */
    reg_eip = registers_get_register("EIP", core);
    reg_epn = registers_get_register("EPN", core);
    reg_ec = registers_get_register("EC", core);
    reg_ema = registers_get_register("EMA", core);

    // Fetch IP stored in EIP
    word_store_integer(reg_eip, curr_ip);
    word_store_integer(reg_ec, code);

    switch (code)
    {
    case EXP_ILLMEM:
        word_store_integer(reg_ema, exception_get_ma());
        word_store_string(reg_epn, "");
        break;

    case EXP_PAGEFAULT:
        word_store_string(reg_ema, "");
        word_store_integer(reg_epn, exception_get_epn());
        break;

    default:
        word_store_string(reg_ema, "");
        word_store_string(reg_epn, "");
    }

    if (mode == PRIVILEGE_USER)
    {
        machine_execute_interrupt_do(XSM_INTERRUPT_EXHANDLER);
        return XSM_SUCCESS;
    }

    fprintf(stderr, "-----------------------------------\n");

    if (_theoptions.debug)
    {
        fprintf(stderr, "%s: Entering Debug Mode.\n", message);
        debug_show_interface();
    }
    else
        fprintf(stderr, "%s.\n", message);

    return XSM_FAILURE;
}

/* To be decided */
void machine_get_mem_access(int *mem_left, int *mem_right)
{
    *mem_left = _thecpu.mem_left;
    *mem_right = _thecpu.mem_right;
}

/* Actions before instruction execution */
void machine_pre_execute(int ip_val)
{
    /* Debug if activated */
    if (_theoptions.debug)
        debug_next_step(ip_val);

    /* Clear the potential watchpoint trigger */
    _thecpu.mem_left = -1;
}

/* Actions after instruction execution */
void machine_post_execute()
{
    int core;
    xsm_word *dest_port;

    core = machine_get_core();

    if (_thecpu.timer[core] >= 0)
        _thecpu.timer[core]--;

    if (_thecpu.disk_wait > 0)
        _thecpu.disk_wait--;

    if (_thecpu.console_wait > 0)
        _thecpu.console_wait--;

    if (_thecpu.timer[core] == 0)
    {
        machine_execute_interrupt_do(XSM_INTERRUPT_TIMER);
        _thecpu.timer[core] = _theoptions.timer;
    }
    else if (_thecpu.disk_state == XSM_DISK_BUSY)
    {
        if (_thecpu.disk_wait == 0 && machine_get_core() == PRIMARY_CORE)
        {
            if (_thecpu.disk_op.operation == XSM_DISKOP_LOAD)
            {
                machine_execute_load_do(_thecpu.disk_op.dest_page, _thecpu.disk_op.src_block);
                machine_execute_interrupt_do(XSM_INTERRUPT_DISK);
            }
            else if (_thecpu.disk_op.operation == XSM_DISKOP_STORE)
            {
                machine_execute_store_do(_thecpu.disk_op.dest_page, _thecpu.disk_op.src_block);
                machine_execute_interrupt_do(XSM_INTERRUPT_DISK);
            }

            _thecpu.disk_state = XSM_DISK_IDLE;
        }
    }
    else if (_thecpu.console_state == XSM_CONSOLE_BUSY)
    {
        if (_thecpu.console_wait == 0 && machine_get_core() == PRIMARY_CORE)
        {
            if (_thecpu.console_op.operation == XSM_CONSOLE_PRINT)
            {
                machine_execute_print_do(&_thecpu.console_op.word);
                machine_execute_interrupt_do(XSM_INTERRUPT_CONSOLE);
            }
            else if (_thecpu.console_op.operation == XSM_CONSOLE_READ)
            {
                machine_execute_in_do(&_thecpu.console_op.word);
                dest_port = registers_get_register("P0", PRIMARY_CORE);
                word_copy(dest_port, &_thecpu.console_op.word);
                machine_execute_interrupt_do(XSM_INTERRUPT_CONSOLE);
            }

            _thecpu.console_state = XSM_CONSOLE_IDLE;
        }
    }
}

/* Call the function based on the given opcode */
int machine_execute_instruction(int opcode)
{
    switch (opcode)
    {
    case MOV:
    case PORT:
        machine_execute_mov();
        break;

    case ADD:
    case SUB:
    case MUL:
    case DIV:
    case MOD:
        machine_execute_arith(opcode);
        break;

    case INR:
    case DCR:
        machine_execute_unary(opcode);
        break;

    case LT:
    case GT:
    case EQ:
    case NE:
    case GE:
    case LE:
        machine_execute_logical(opcode);
        break;

    case JZ:
    case JNZ:
    case JMP:
        machine_execute_jump(opcode);
        break;

    case PUSH:
    case POP:
        machine_execute_stack(opcode);
        break;

    case CALL:
        machine_execute_call();
        break;

    case RET:
        machine_execute_ret();
        break;

    case BRKP:
        machine_execute_brkp();
        break;

    case INT:
        machine_execute_interrupt();
        break;

    case LOAD:
        machine_execute_disk(XSM_DISKOP_LOAD, FALSE);
        break;

    case LOADI:
        machine_execute_disk(XSM_DISKOP_LOAD, TRUE);
        break;

    case STORE:
        machine_execute_disk(XSM_DISKOP_STORE, FALSE);
        break;

    case ENCRYPT:
        machine_execute_encrypt();
        break;

    case BACKUP:
        machine_execute_backup();
        break;

    case RESTORE:
        machine_execute_restore();
        break;

    case IN:
        machine_schedule_in(_theoptions.console);
        break;

    case INI:
        machine_execute_ini();
        break;

    case OUT:
        machine_execute_print();
        break;

    case IRET:
        machine_execute_iret();
        break;

    case TSL:
        machine_execute_tsl();
        break;

    case START:
        machine_execute_start();
        break;

    case RESET:
        machine_execute_reset();
        break;

    case HALT:
        return XSM_HALT;

    case NOP:
        // Do nothing
        break;
    }

    return TRUE;
}

/* Returns the word stored in the instruction */
xsm_word *machine_get_address(int write)
{
    int address = machine_get_address_int(write);
    return machine_memory_get_word(address);
}

/* Returns the address in the instruction */
int machine_get_address_int(int write)
{
    int token, address, ret_addr, core;
    YYSTYPE token_info;

    core = machine_get_core();

    /* Skip the opening square bracket */
    tokenize_next_token(&token_info);
    token = tokenize_next_token(&token_info);

    switch (token)
    {
    case TOKEN_REGISTER:
        address = registers_get_integer(token_info.str, core);
        break;

    case TOKEN_NUMBER:
        address = token_info.val;
        break;

    default:
        machine_register_exception("Invalid memory derefence", EXP_ILLINSTR);
    }

    /* Neglect the closing bracket */
    tokenize_next_token(&token_info);

    ret_addr = machine_translate_address(address, write, OPER_FETCH, machine_get_core(), machine_get_mode());
    return ret_addr;
}

/* Translate the logical address */
int machine_translate_address(int address, int write, int type, int core, int mode)
{
    int ptbr, ptlr, ret_addr, curr_ip;

    if (mode == PRIVILEGE_KERNEL)
        return address;

    ptbr = word_get_integer(registers_get_register("PTBR", core));
    ptlr = word_get_integer(registers_get_register("PTLR", core));
    ret_addr = memory_translate_address(ptbr, ptlr, address, write);

    if (ret_addr < 0 && type == DEBUG_FETCH)
        return ret_addr;

    if (ret_addr < 0 && type == INSTR_FETCH)
    {
        curr_ip = word_get_integer(machine_get_ipreg());
        curr_ip = curr_ip + XSM_INSTRUCTION_SIZE;
        word_store_integer(machine_get_ipreg(), curr_ip);
    }

    if (XSM_MEM_NOWRITE == ret_addr)
    {
        exception_set_ma(address);
        machine_register_exception("Access violation", EXP_ILLMEM);
    }

    else if (XSM_MEM_PAGEFAULT == ret_addr)
    {
        exception_set_epn(memory_addr_page(address));
        machine_register_exception("Page fault", EXP_PAGEFAULT);
    }

    else if (XSM_MEM_ILLPAGE == ret_addr)
    {
        exception_set_ma(address);
        machine_register_exception("Address outside logical address space", EXP_ILLMEM);
    }

    return ret_addr;
}

/* Retrieve the word in the given address from memory */
xsm_word *machine_memory_get_word(int address)
{
    xsm_word *result = memory_get_word(address);

    if (result == NULL)
    {
        exception_set_ma(address);
        machine_register_exception("Illegal memory access", EXP_ILLMEM);
    }

    return result;
}

/* Execute MOV/PORT instructions */
int machine_execute_mov()
{
    int token, mem_write_addr;
    xsm_word *l_address, *r_address;
    YYSTYPE token_info;

    token = tokenize_peek(&token_info);

    switch (token)
    {
    case TOKEN_DREF_L:
        _thecpu.mem_left = machine_get_address_int(TRUE);
        _thecpu.mem_right = _thecpu.mem_right;
        l_address = machine_memory_get_word(_thecpu.mem_left);
        break;

    case TOKEN_REGISTER:
        l_address = machine_get_register(token_info.str);
        token = tokenize_next_token(&token_info);
        break;
    }

    token = tokenize_next_token(&token_info);

    if (token != TOKEN_COMMA)
        machine_register_exception("Malformed instruction", EXP_ILLINSTR);

    token = tokenize_peek(&token_info);

    switch (token)
    {
    case TOKEN_DREF_L:
        r_address = machine_get_address(FALSE);
        word_copy(l_address, r_address);
        break;

    case TOKEN_REGISTER:
        r_address = machine_read_register(token_info.str);
        word_copy(l_address, r_address);
        tokenize_next_token(&token_info);
        break;

    case TOKEN_NUMBER:
        word_store_integer(l_address, token_info.val);
        tokenize_next_token(&token_info);
        break;

    case TOKEN_STRING:
        word_store_string(l_address, token_info.str);
        tokenize_next_token(&token_info);
        break;
    }

    return XSM_SUCCESS;
}

/* Execute arithmetic instructions */
int machine_execute_arith(int opcode)
{
    int result, token, l_value, r_value;
    xsm_reg *l_operand, *r_operand;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);

    if (token != TOKEN_REGISTER)
        machine_register_exception("Wrong operand", EXP_ILLINSTR);

    l_operand = machine_get_register(token_info.str);
    l_value = word_get_integer(l_operand);

    /* Neglect comma */
    tokenize_next_token(&token_info);
    token = tokenize_next_token(&token_info);

    if (token == TOKEN_NUMBER)
        r_value = token_info.val;
    else
    {
        r_operand = machine_read_register(token_info.str);
        r_value = word_get_integer(r_operand);
    }

    switch (opcode)
    {
    case ADD:
        result = r_value + l_value;
        break;

    case SUB:
        result = l_value - r_value;
        break;

    case MUL:
        result = l_value * r_value;
        break;

    case DIV:
        if (r_value == 0)
            machine_register_exception("Integer division by zero", EXP_ARITH);
        result = l_value / r_value;
        break;

    case MOD:
        if (r_value == 0)
            machine_register_exception("Integer modulo by zero", EXP_ARITH);
        result = l_value % r_value;
        break;
    }

    word_store_integer(l_operand, result);
    return XSM_SUCCESS;
}

/* Execute unary instructions */
int machine_execute_unary(int opcode)
{
    int token, val;
    YYSTYPE token_info;
    xsm_word *arg_reg;

    token = tokenize_next_token(&token_info);
    arg_reg = machine_get_register(token_info.str);

    val = word_get_integer(arg_reg);

    switch (opcode)
    {
    case INR:
        val = val + 1;
        break;

    case DCR:
        val = val - 1;
        break;
    }

    word_store_integer(arg_reg, val);
    return XSM_SUCCESS;
}

/* Execute logical instructions */
int machine_execute_logical(int opcode)
{
    xsm_word *src_left_reg, *src_right_reg;
    int token;
    YYSTYPE token_info;

    int result, val_left, val_right;

    token = tokenize_next_token(&token_info);
    src_left_reg = machine_get_register(token_info.str);

    /* Comma */
    token = tokenize_next_token(&token_info);

    if (TOKEN_COMMA != token)
    {
        machine_register_exception("Incorrect logical instruction", EXP_ILLINSTR);
    }

    token = tokenize_next_token(&token_info);
    src_right_reg = machine_read_register(token_info.str);

    /* String operation */
    if (word_get_unix_type(src_left_reg) == XSM_TYPE_STRING || word_get_unix_type(src_right_reg) == XSM_TYPE_STRING)
    {
        char *wor_left = word_get_string(src_left_reg);
        char *wor_right = word_get_string(src_right_reg);

        switch (opcode)
        {
        case LT:
            result = strcmp(wor_left, wor_right) < 0 ? 1 : 0;
            break;

        case GT:
            result = strcmp(wor_left, wor_right) > 0 ? 1 : 0;
            break;

        case EQ:
            result = strcmp(wor_left, wor_right) == 0 ? 1 : 0;
            break;

        case NE:
            result = strcmp(wor_left, wor_right) != 0 ? 1 : 0;
            break;

        case GE:
            result = strcmp(wor_left, wor_right) >= 0 ? 1 : 0;
            break;

        case LE:
            result = strcmp(wor_left, wor_right) <= 0 ? 1 : 0;
            break;
        }
    }
    else /* Integer operation */
    {
        val_left = word_get_integer(src_left_reg);
        val_right = word_get_integer(src_right_reg);

        switch (opcode)
        {
        case LT:
            result = val_left < val_right ? 1 : 0;
            break;

        case GT:
            result = val_left > val_right ? 1 : 0;
            break;

        case EQ:
            result = val_left == val_right ? 1 : 0;
            break;

        case NE:
            result = val_left != val_right ? 1 : 0;
            break;

        case GE:
            result = val_left >= val_right ? 1 : 0;
            break;

        case LE:
            result = val_left <= val_right ? 1 : 0;
            break;
        }
    }

    word_store_integer(src_left_reg, result);
    return XSM_SUCCESS;
}

/* Execute jump instructions */
int machine_execute_jump(int opcode)
{
    int test, target, token, core;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);
    core = machine_get_core();

    if (token == TOKEN_NUMBER)
    {
        test = TRUE;
        target = token_info.val;
    }
    else
    {
        // String content is true
        if (word_get_unix_type(machine_read_register(token_info.str)) == XSM_TYPE_STRING)
            test = 1;
        else
            test = word_get_integer(machine_read_register(token_info.str));

        /* Skip the comma */
        tokenize_next_token(&token_info);
        token = tokenize_next_token(&token_info);
        target = token_info.val;
    }

    if (JZ == opcode)
        test = !test;

    // Unconditional jump
    if (test)
        word_store_integer(registers_get_register("IP", core), target);

    return XSM_SUCCESS;
}

/* Execute PUSH/POP instructions */
int machine_execute_stack(int opcode)
{
    int token;
    xsm_word *reg;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);

    if (token == TOKEN_REGISTER)
        reg = machine_read_register(token_info.str);
    else
        machine_register_exception("Stack instructions require a register as argument", EXP_ILLINSTR);

    switch (opcode)
    {
    case PUSH:
        return machine_push_do(reg);

    case POP:
        return machine_pop_do(reg);
    }

    return XSM_SUCCESS;
}

/* Execute PUSH instruction */
int machine_push_do(xsm_word *reg)
{
    int stack_top;
    xsm_word *xw_stack_top, *sp_reg;

    /* Update SP */
    sp_reg = machine_get_spreg();
    stack_top = word_get_integer(sp_reg);
    word_store_integer(sp_reg, stack_top + 1);

    /* Get the new stack pointer */
    xw_stack_top = machine_stack_pointer(TRUE);

    /* Put the word on the top of stack */
    word_copy(xw_stack_top, reg);
    return XSM_SUCCESS;
}

/* Execute POP instruction */
int machine_pop_do(xsm_word *dest)
{
    int stack_top;
    xsm_word *xw_stack_top, *sp_reg;

    /* Get the stack pointer */
    xw_stack_top = machine_stack_pointer(FALSE);
    sp_reg = machine_get_spreg();
    stack_top = word_get_integer(sp_reg);

    /* Copy the word from the top of stack */
    word_copy(dest, xw_stack_top);

    /* Update SP */
    word_store_integer(sp_reg, stack_top - 1);
    return XSM_SUCCESS;
}

/* Retrieve the word pointed by SP */
xsm_word *machine_stack_pointer(int write)
{
    int stack_top;
    xsm_word *sp_reg = machine_get_spreg();

    stack_top = word_get_integer(sp_reg);
    stack_top = machine_translate_address(stack_top, write, OPER_FETCH, machine_get_core(), machine_get_mode());

    if (write)
        _thecpu.mem_left = stack_top;

    return machine_memory_get_word(stack_top);
}

/* Execute CALL target instruction */
int machine_execute_call_do(int target)
{
    int curr_ip, curr_sp;
    xsm_word *ipreg, *spreg, *stack_pointer;

    /* Increment SP. */
    spreg = machine_get_spreg();
    curr_sp = word_get_integer(spreg);
    curr_sp = curr_sp + 1;
    word_store_integer(spreg, curr_sp);

    /* Save IP onto the stack. */
    ipreg = machine_get_ipreg();
    curr_ip = word_get_integer(ipreg);
    stack_pointer = machine_stack_pointer(TRUE);
    word_store_integer(stack_pointer, curr_ip);

    /* Update IP to the new code location. */
    word_store_integer(ipreg, target);
    return XSM_SUCCESS;
}

/* Execute CALL instruction */
int machine_execute_call()
{
    int token, target;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);

    if (token == TOKEN_NUMBER)
        target = token_info.val;
    else
        target = word_get_integer(machine_read_register(token_info.str));

    return machine_execute_call_do(target);
}

/* Execute RET instruction */
int machine_execute_ret()
{
    int target, curr_sp;
    xsm_word *spreg, *ipreg;
    xsm_word *stack_pointer;

    spreg = machine_get_spreg();
    stack_pointer = machine_stack_pointer(FALSE);
    target = word_get_integer(stack_pointer);

    curr_sp = word_get_integer(spreg);
    curr_sp = curr_sp - 1;
    word_store_integer(spreg, curr_sp);

    ipreg = machine_get_ipreg();
    word_store_integer(ipreg, target);

    return XSM_SUCCESS;
}

/* Execute BRKP instruction */
int machine_execute_brkp()
{
    /* If debug mode is not enabled, neglect this instruction. */
    if (!_theoptions.debug)
        return XSM_SUCCESS;

    // Activate debugger
    debug_activate();

    return XSM_SUCCESS;
}

/* Execute INT instruction */
int machine_execute_interrupt()
{
    int token, interrupt_num;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);
    interrupt_num = token_info.val;

    if (interrupt_num < INTERRUPT_LOW || interrupt_num > INTERRUPT_HIGH)
        machine_register_exception("Invalid interrupt number", EXP_ILLINSTR);

    return machine_execute_interrupt_do(interrupt_num);
}

/* Execute INT interrupt instruction */
int machine_execute_interrupt_do(int interrupt)
{
    int target;

    if (machine_get_mode() == PRIVILEGE_KERNEL)
        machine_register_exception("Invoking interrupts in kernel mode not allowed", EXP_ILLINSTR);

    target = machine_interrupt_address(interrupt);

    if (interrupt != XSM_INTERRUPT_EXHANDLER)
        machine_execute_call_do(target);
    else
        word_store_integer(machine_get_ipreg(), target);

    /* Change the mode now, that will do. */
    machine_set_mode(PRIVILEGE_KERNEL);
    return XSM_SUCCESS;
}

/* Retrieve the starting address of interrupt */
int machine_interrupt_address(int int_num)
{
    if (int_num > INTERRUPT_HIGH)
        return -1;

    if (int_num == 19)
        return 130 * XSM_PAGE_SIZE;
    else
        return (int_num * 2 + 2) * XSM_PAGE_SIZE;
}

/* Execute LOAD/STORE instructions */
int machine_execute_disk(int operation, int immediate)
{
    int page_num, block_num;
    xsm_word *page_base;

    page_num = machine_read_disk_arg();
    if (page_num <= 0 || page_num >= XSM_MEMORY_NUMPAGES)
        machine_register_exception("Invalid page number for disk instruction", EXP_ILLINSTR);

    /* Neglect comma */
    tokenize_skip_token();

    block_num = machine_read_disk_arg();
    if (block_num < 0 || block_num >= XSM_DISK_BLOCK_NUM)
        machine_register_exception("Invalid block number for disk instruction", EXP_ILLINSTR);

    if (immediate)
    {
        if (operation == XSM_DISKOP_LOAD)
            return machine_execute_load_do(page_num, block_num);
        else if (operation == XSM_DISKOP_STORE)
            return machine_execute_store_do(page_num, block_num);
    }
    else
        return machine_schedule_disk(page_num, block_num, _theoptions.disk, operation);

    return XSM_SUCCESS;
}

/* Return the disk instruction arguments */
int machine_read_disk_arg()
{
    int token;
    xsm_word *reg;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);

    if (token == TOKEN_NUMBER)
        return token_info.val;
    else if (token == TOKEN_REGISTER)
    {
        reg = machine_read_register(token_info.str);
        return word_get_integer(reg);
    }
    else
        machine_register_exception("Wrong arguments for disk instruction", EXP_ILLINSTR);

    return 0;
}

/* Schedule DISK_BUSY */
int machine_schedule_disk(int page_num, int block_num, int firetime, int operation)
{
    /* If the disk is busy, ignore the request */
    if (_thecpu.disk_state == XSM_DISK_BUSY)
        return XSM_SUCCESS;

    _thecpu.disk_state = XSM_DISK_BUSY;
    _thecpu.disk_wait = firetime;
    _thecpu.disk_op.src_block = block_num;
    _thecpu.disk_op.dest_page = page_num;
    _thecpu.disk_op.operation = operation;

    return XSM_SUCCESS;
}

/* Execute LOAD instruction */
int machine_execute_load_do(int page_num, int block_num)
{
    xsm_word *page_base = memory_get_page(page_num);
    return disk_read_block(page_base, block_num);
}

/* Execute STORE instruction */
int machine_execute_store_do(int page_num, int block_num)
{
    xsm_word *page_base = memory_get_page(page_num);
    return disk_write_page(page_base, block_num);
}

/* Execute ENCRYPT instruction */
int machine_execute_encrypt()
{
    int token;
    xsm_word *reg;
    YYSTYPE token_info;

    token = tokenize_next_token(&token_info);
    reg = machine_get_register(token_info.str);

    /* Some very easy encryption */
    word_encrypt(reg);

    return XSM_SUCCESS;
}

/* Execute BACKUP instruction */
int machine_execute_backup()
{
    int ireg, core;
    char str_reg[5];
    xsm_word *reg;

    core = machine_get_core();
    reg = registers_get_register("BP", core);
    machine_push_do(reg);

    for (ireg = 0; ireg < REG_COUNT; ++ireg)
    {
        sprintf(str_reg, "R%d", ireg);
        reg = registers_get_register(str_reg, core);
        machine_push_do(reg);
    }

    return XSM_SUCCESS;
}

/* Execute RESTORE instruction */
int machine_execute_restore()
{
    int ireg, core;
    char str_reg[5];
    xsm_word *reg;

    core = machine_get_core();

    for (ireg = REG_COUNT - 1; ireg >= 0; ireg--)
    {
        sprintf(str_reg, "R%d", ireg);
        reg = registers_get_register(str_reg, core);
        machine_pop_do(reg);
    }

    reg = registers_get_register("BP", core);
    machine_pop_do(reg);

    return XSM_SUCCESS;
}

/* Execute OUT word instruction */
int machine_execute_print_do(xsm_word *word)
{
    int type, val;
    char *str;

    type = word_get_unix_type(word);

    if (type == XSM_TYPE_STRING)
    {
        str = word_get_string(word);
        fprintf(stdout, "%s\n", str);
    }
    else
    {
        val = word_get_integer(word);
        fprintf(stdout, "%d\n", val);
    }

    return XSM_SUCCESS;
}

/* Execute OUT instruction */
int machine_execute_print()
{
    int val, core;
    core = machine_get_core();
    xsm_word *reg = registers_get_register("P1", core);
    return machine_execute_print_do(reg);
}

/* Execute IN instruction */
int machine_schedule_in(int firetime)
{
    if (_thecpu.console_state == XSM_CONSOLE_BUSY)
        return XSM_FAILURE;

    _thecpu.console_op.operation = XSM_CONSOLE_READ;
    _thecpu.console_state = XSM_CONSOLE_BUSY;
    _thecpu.console_wait = firetime;

    return XSM_SUCCESS;
}

/* Execute INI instruction */
int machine_execute_ini()
{
    int core;
    xsm_word *reg;

    core = machine_get_core();

    if (!_theoptions.debug)
        return XSM_SUCCESS;

    reg = registers_get_register("P0", core);
    return machine_execute_in_do(reg);
}

/* Execute IN word instruction */
int machine_execute_in_do(xsm_word *word)
{
    int i;
    char input[XSM_WORD_SIZE];

    fgets(input, XSM_WORD_SIZE, stdin);

    /* Kill the extra newline. */
    for (i = 0; i < XSM_WORD_SIZE; ++i)
        if (input[i] == '\n')
            input[i] = '\0';

    return word_store_string(word, input);
}

/* Execute IRET instruction */
int machine_execute_iret()
{
    xsm_word target, *ipreg;

    machine_set_mode(PRIVILEGE_USER);
    machine_pop_do(&target);

    ipreg = machine_get_ipreg();
    word_copy(ipreg, &target);
    return XSM_SUCCESS;
}

/* Execute TSL instruction */
int machine_execute_tsl()
{
    int token;
    xsm_word *l_address, *r_address;
    YYSTYPE token_info;

    token = tokenize_peek(&token_info);

    switch (token)
    {
    case TOKEN_REGISTER:
        l_address = machine_get_register(token_info.str);
        tokenize_next_token(&token_info);
        break;

    default:
        machine_register_exception("Malformed instruction", EXP_ILLINSTR);
    }

    token = tokenize_next_token(&token_info);

    if (token != TOKEN_COMMA)
        machine_register_exception("Malformed instruction", EXP_ILLINSTR);

    token = tokenize_peek(&token_info);

    switch (token)
    {
    case TOKEN_DREF_L:
        _thecpu.mem_left = machine_get_address_int(TRUE);
        _thecpu.mem_right = _thecpu.mem_right;
        r_address = machine_memory_get_word(_thecpu.mem_left);
        word_copy(l_address, r_address);
        word_store_integer(r_address, 1);
        break;

    default:
        machine_register_exception("Malformed instruction", EXP_ILLINSTR);
    }

    return XSM_SUCCESS;
}

/* Execute START instruction */
int machine_execute_start()
{
    int state = machine_get_core_state();

    if (state == RESET_MODE)
        machine_init_dual();

    return XSM_SUCCESS;
}

/* Execute RESET instruction */
int machine_execute_reset()
{
    int state = machine_get_core_state();

    if (state == ACTIVE_MODE)
        machine_set_core_state(RESET_MODE);

    return XSM_SUCCESS;
}

/* Returns the mode */
int machine_get_mode()
{
    int core = machine_get_core();
    return _thecpu.mode[core];
}

/* Set the mode */
void machine_set_mode(int mode)
{
    int core = machine_get_core();
    _thecpu.mode[core] = mode;
}

/* Returns the core */
int machine_get_core()
{
    return _thecpu.core;
}

/* Set the core */
void machine_set_core(int core)
{
    _thecpu.core = core;
}

/* Returns the secondary core mode */
int machine_get_core_state()
{
    return _thecpu.core_state;
}

/* Set the secondary core mode */
void machine_set_core_state(int mode)
{
    _thecpu.core_state = mode;
}

/* Deallocate the machine */
void machine_destroy()
{
    memory_destroy();
    registers_destroy();
}