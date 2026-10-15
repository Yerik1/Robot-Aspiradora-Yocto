#ifndef RPC_HANDLERS_H
#define RPC_HANDLERS_H

#include "../mongoose/mongoose.h"

/**
 * Registers all JSON-RPC methods into the Mongoose RPC dispatcher head.
 */
/* Marca en c->data[1] de una conexion WebSocket con sesion iniciada.
 * La sesion vive mientras dure la conexion (c->data[0] == 'W'). */
#define CONN_AUTH_FLAG 'A'

void rpc_handlers_init(struct mg_rpc **head);

#endif /* RPC_HANDLERS_H */
