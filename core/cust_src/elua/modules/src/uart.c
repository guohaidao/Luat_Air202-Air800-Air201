// Module for interfacing with UART

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "auxmods.h"
#include "lrotable.h"
#include "common.h"
#include "sermux.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "platform_conf.h"

// Modes for the UART read function
enum
{
  UART_READ_MODE_LINE,
  UART_READ_MODE_NUMBER,
  UART_READ_MODE_SPACE,
  UART_READ_MODE_MAXSIZE
};

static int uart_setup( lua_State* L )
{
  unsigned id, databits, parity, stopbits;
  u32 baud, res;
  u32 mode;
  u32 txDoneReport;
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( uart, id );
  if( id >= SERMUX_SERVICE_ID_FIRST && id != PLATFORM_UART_ID_ATC)
    return luaL_error( L, "uart.setup can't be called on virtual UARTs" );
  baud = luaL_checkinteger( L, 2 );
  databits = luaL_checkinteger( L, 3 );
  parity = luaL_checkinteger( L, 4 );
  stopbits = luaL_checkinteger( L, 5 );
  /*+\NEW\liweiqiang\2013.4.22\����uart��Ϣ��ʾ������ѯ��ȡ����ѡ�� */
  mode = lua_tointeger(L, 6);
  /*-\NEW\liweiqiang\2013.4.22\����uart��Ϣ��ʾ������ѯ��ȡ����ѡ�� */

  txDoneReport = lua_tointeger(L, 7);
  
  res = platform_uart_setup( id, baud, databits, parity, stopbits, mode, txDoneReport);

  if(res != baud)
  {
    return luaL_error( L, "uart.setup error ret(%d)", res );
  }
  
  lua_pushinteger( L, res );
  return 1;
}

/*+\NEW\liweiqiang\2013.4.20\����uart.close�ӿ� */
// Lua: res = close( id )
static int uart_close( lua_State* L )
{
    u32 id = luaL_checkinteger(L,1);
    u32 res;

    res = platform_uart_close(id);

    lua_pushinteger(L, res);

    return 1;
}
/*-\NEW\liweiqiang\2013.4.20\����uart.close�ӿ� */

// Lua: write( id, string1, [string2], ..., [stringn] )
static int uart_write( lua_State* L )
{
  int id;
  const char* buf;
  size_t len;
  int total = lua_gettop( L ), s;
  u32 wrote = 0;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( uart, id );
  for( s = 2; s <= total; s ++ )
  {
    if( lua_type( L, s ) == LUA_TNUMBER )
    {
      len = lua_tointeger( L, s );
      if( ( len < 0 ) || ( len > 255 ) )
        return luaL_error( L, "invalid number" );
      wrote += platform_uart_send( id, ( u8 )len );
    }
    else
    {
      luaL_checktype( L, s, LUA_TSTRING );
      buf = lua_tolstring( L, s, &len );
      wrote += platform_uart_send_buff(id, buf, len);
    }
  }

  lua_pushinteger(L, wrote);
  
  return 1;
}

// Lua: write( id, string1, [string2], ..., [stringn] )
static int uart_sync_write( lua_State* L )
{
  int id;
  const char* buf;
  size_t len;
  int total = lua_gettop( L ), s;
  u32 wrote = 0;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( uart, id );
  for( s = 2; s <= total; s ++ )
  {
    if( lua_type( L, s ) == LUA_TNUMBER )
    {
      len = lua_tointeger( L, s );
      if( ( len < 0 ) || ( len > 255 ) )
        return luaL_error( L, "invalid number" );
      wrote += platform_uart_sync_send( id, ( u8 )len );
    }
    else
    {
      luaL_checktype( L, s, LUA_TSTRING );
      buf = lua_tolstring( L, s, &len );
      wrote += platform_uart_send_buff(id, buf, len);
    }
  }

  lua_pushinteger(L, wrote);
  
  return 1;
}

static int uart_read( lua_State* L )
{
  int id, res, mode, issign;
  unsigned timer_id = 0;
  s32 timeout = PLATFORM_UART_INFINITE_TIMEOUT, maxsize = 0, count = 0;
  const char *fmt;
  luaL_Buffer b;
  char cres;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( uart, id );

  // Check format
  if( lua_isnumber( L, 2 ) )
  {
    if( ( maxsize = ( s32 )lua_tointeger( L, 2 ) ) < 0 )
      return luaL_error( L, "invalid max size" );
    mode = UART_READ_MODE_MAXSIZE;
  }
  else
  {
    fmt = luaL_checkstring( L, 2 );
    if( !strcmp( fmt, "*l" ) )
      mode = UART_READ_MODE_LINE;
    else if( !strcmp( fmt, "*n" ) )
      mode = UART_READ_MODE_NUMBER;
    else if( !strcmp( fmt, "*s" ) )
      mode = UART_READ_MODE_SPACE;
    else
      return luaL_error( L, "invalid format" );
  }

  // Check timeout and timer id
  if( lua_gettop( L ) >= 3 )
  {
    timeout = luaL_checkinteger( L, 3 );
    if( ( timeout < 0 ) && ( timeout != PLATFORM_UART_INFINITE_TIMEOUT ) )
      return luaL_error( L, "invalid timeout value" );      
    if( ( timeout != PLATFORM_UART_INFINITE_TIMEOUT ) && ( timeout != 0 ) )
      timer_id = luaL_checkinteger( L, 4 );    
  }

  // Read data
  luaL_buffinit( L, &b );
  while( 1 )
  {
    if( ( res = platform_uart_recv( id, timer_id, timeout ) ) == -1 )
      break; 
    cres = ( char )res;
    count ++;
    issign = ( count == 1 ) && ( ( res == '-' ) || ( res == '+' ) );
    // [TODO] this only works for lines that actually end with '\n', other line endings
    // are not supported.
    if( ( cres == '\n' ) && ( mode == UART_READ_MODE_LINE ) )
    {
        luaL_putchar( &b, cres );
        break;
    }
    if( !isdigit( cres ) && !issign && ( mode == UART_READ_MODE_NUMBER ) )
      break;
    if( isspace( cres ) && ( mode == UART_READ_MODE_SPACE ) )
      break;
    luaL_putchar( &b, cres );
    if( ( count == maxsize ) && ( mode == UART_READ_MODE_MAXSIZE ) )
      break;
  }
  luaL_pushresult( &b );

  // Return an integer if needed
  if( mode == UART_READ_MODE_NUMBER )
  {
    res = lua_tointeger( L, -1 );
    lua_pop( L, 1 );
    lua_pushinteger( L, res );
  }
  return 1;  
}

// Lua: data = getchar( id, [ timeout ], [ timer_id ] )
static int uart_getchar( lua_State* L )
{
  int id, res;
  char cres;
  unsigned timer_id = 0;
  s32 timeout = PLATFORM_UART_INFINITE_TIMEOUT;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( uart, id );

  // Check timeout and timer id
  if( lua_gettop( L ) >= 2 )
  {
    timeout = luaL_checkinteger( L, 2 );
    if( ( timeout < 0 ) && ( timeout != PLATFORM_UART_INFINITE_TIMEOUT ) )
      return luaL_error( L, "invalid timeout value" );      
    if( ( timeout != PLATFORM_UART_INFINITE_TIMEOUT ) && ( timeout != 0 ) )
      timer_id = luaL_checkinteger( L, 3 );    
  }
  res = platform_uart_recv( id, timer_id, timeout );
  if( res == -1 )
    lua_pushstring( L, "" );
  else
  {
    cres = ( char )res;
    lua_pushlstring( L, &cres, 1 );
  }
  return 1;  
}


// Lua: uart.set_buffer( id, size )
static int uart_set_buffer( lua_State *L )
{
  int id = luaL_checkinteger( L, 1 );
  u32 size = ( u32 )luaL_checkinteger( L, 2 );
  
  MOD_CHECK_ID( uart, id );
  if( size && ( size & ( size - 1 ) ) )
    return luaL_error( L, "the buffer size must be a power of 2 or 0" );
  if( size == 0 && id >= SERMUX_SERVICE_ID_FIRST )
    return luaL_error( L, "disabling buffers on virtual UARTs is not allowed" );
  if( platform_uart_set_buffer( id, intlog2( size ) ) == PLATFORM_ERR )
    return luaL_error( L, "unable to set UART buffer" );
  return 0;
}

// Lua: uart.set_flow_control( id, type )
static int uart_set_flow_control( lua_State *L )
{
  int id = luaL_checkinteger( L, 1 );
  int type = luaL_checkinteger( L, 2 );

  MOD_CHECK_ID( uart, id );
  if( platform_uart_set_flow_control( id, type ) != PLATFORM_OK )
    return luaL_error( L, "unable to set the flow control on interface %d", id );
  return 0;
}

static int os_sleep( lua_State *L )
{
    int ms = luaL_checkinteger( L, 1 );
    
    platform_os_sleep(ms);
    
    return 0;
}

#ifdef BUILD_SERMUX

#define MAX_VUART_NAME_LEN    6
#define MIN_VUART_NAME_LEN    6

// __index metafunction for UART
// Look for all VUARTx timer identifiers
static int uart_mt_index( lua_State* L )
{
  const char *key = luaL_checkstring( L ,2 );
  char* pend;
  long res;
  
  if( strlen( key ) > MAX_VUART_NAME_LEN || strlen( key ) < MIN_VUART_NAME_LEN )
    return 0;
  if( strncmp( key, "VUART", 5 ) )
    return 0;  
  res = strtol( key + 5, &pend, 10 );
  if( *pend != '\0' )
    return 0;
  if( res >= SERMUX_NUM_VUART )
    return 0;
  lua_pushinteger( L, SERMUX_SERVICE_ID_FIRST + res );
  return 1;
}
#endif // #ifdef BUILD_SERMUX

// Module function map
#define MIN_OPT_LEVEL   2
#include "lrodefs.h"
const LUA_REG_TYPE uart_map[] = 
{
  { LSTRKEY( "setup" ),  LFUNCVAL( uart_setup ) },
/*+\NEW\liweiqiang\2013.4.20\����uart.close�ӿ� */
  { LSTRKEY( "close" ),  LFUNCVAL( uart_close ) },
/*-\NEW\liweiqiang\2013.4.20\����uart.close�ӿ� */
  { LSTRKEY( "write" ), LFUNCVAL( uart_write ) },
  { LSTRKEY( "sync_write" ), LFUNCVAL( uart_sync_write ) },
  { LSTRKEY( "read" ), LFUNCVAL( uart_read ) },
  { LSTRKEY( "getchar" ), LFUNCVAL( uart_getchar ) },
  { LSTRKEY( "set_buffer" ), LFUNCVAL( uart_set_buffer ) },
  { LSTRKEY( "set_flow_control" ), LFUNCVAL( uart_set_flow_control ) },
  { LSTRKEY( "sleep" ), LFUNCVAL( os_sleep ) },
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "PAR_EVEN" ), LNUMVAL( PLATFORM_UART_PARITY_EVEN ) },
  { LSTRKEY( "PAR_ODD" ), LNUMVAL( PLATFORM_UART_PARITY_ODD ) },
  { LSTRKEY( "PAR_NONE" ), LNUMVAL( PLATFORM_UART_PARITY_NONE ) },
  { LSTRKEY( "STOP_1" ), LNUMVAL( PLATFORM_UART_STOPBITS_1 ) },
  { LSTRKEY( "STOP_1_5" ), LNUMVAL( PLATFORM_UART_STOPBITS_1_5 ) },
  { LSTRKEY( "STOP_2" ), LNUMVAL( PLATFORM_UART_STOPBITS_2 ) },
  { LSTRKEY( "NO_TIMEOUT" ), LNUMVAL( 0 ) },
  { LSTRKEY( "INF_TIMEOUT" ), LNUMVAL( PLATFORM_UART_INFINITE_TIMEOUT ) },
  { LSTRKEY( "FLOW_NONE" ), LNUMVAL( PLATFORM_UART_FLOW_NONE ) },
  { LSTRKEY( "FLOW_RTS" ), LNUMVAL( PLATFORM_UART_FLOW_RTS ) },
  { LSTRKEY( "FLOW_CTS" ), LNUMVAL( PLATFORM_UART_FLOW_CTS ) },
#endif
#if LUA_OPTIMIZE_MEMORY > 0 && defined( BUILD_SERMUX )
  { LSTRKEY( "__metatable" ), LROVAL( uart_map ) },
  { LSTRKEY( "__index" ), LFUNCVAL( uart_mt_index ) },  
#endif
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_uart( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else // #if LUA_OPTIMIZE_MEMORY > 0
  luaL_register( L, AUXLIB_UART, uart_map );
  
  MOD_REG_NUMBER( L, "ATC", PLATFORM_UART_ID_ATC );
  
  // Add the stop bits and parity constants (for uart.setup)
  MOD_REG_NUMBER( L, "PAR_EVEN", PLATFORM_UART_PARITY_EVEN );
  MOD_REG_NUMBER( L, "PAR_ODD", PLATFORM_UART_PARITY_ODD );
  MOD_REG_NUMBER( L, "PAR_NONE", PLATFORM_UART_PARITY_NONE );
  MOD_REG_NUMBER( L, "STOP_1", PLATFORM_UART_STOPBITS_1 );
  MOD_REG_NUMBER( L, "STOP_1_5", PLATFORM_UART_STOPBITS_1_5 );
  MOD_REG_NUMBER( L, "STOP_2", PLATFORM_UART_STOPBITS_2 );
  
  // Add the "none" and "infinite" constant used in recv()
  MOD_REG_NUMBER( L, "NO_TIMEOUT", 0 );
  MOD_REG_NUMBER( L, "INF_TIMEOUT", PLATFORM_UART_INFINITE_TIMEOUT );

  // Add the UART flow constants
  MOD_REG_NUMBER( L, "FLOW_RTS", PLATFORM_UART_FLOW_RTS );
  MOD_REG_NUMBER( L, "FLOW_CTS", PLATFORM_UART_FLOW_CTS );
  
  return 1;
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}
