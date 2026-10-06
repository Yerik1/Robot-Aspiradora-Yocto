#ifndef RPC_HANDLERS_H
#define RPC_HANDLERS_H

#include "../mongoose/mongoose.h"

/**
 * Registers all JSON-RPC methods into the Mongoose RPC dispatcher head.
 */
void rpc_handlers_init(struct mg_rpc **head);

#endif /* RPC_HANDLERS_H */
