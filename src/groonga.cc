#include <v8.h>
#include <node.h>
#include <groonga/groonga.h>
#include "groonga.h"

namespace node_groonga {

using namespace v8;

static Persistent<FunctionTemplate> groonga_context_constructor;

void Database::Initialize(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(Database::New);
  groonga_context_constructor = Persistent<FunctionTemplate>::New(t);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Database"));

  NODE_SET_PROTOTYPE_METHOD(t, "command", Database::Command);
  NODE_SET_PROTOTYPE_METHOD(t, "commandSync", Database::CommandSync);

  target->Set(String::NewSymbol("Database"), t->GetFunction());
}

Handle<Value> Database::New(const Arguments& args) {
  HandleScope scope;

  if (!args.IsConstructCall()) {
    return ThrowException(Exception::TypeError(
          String::New("Use the new operator to create new Database objects"))
        );
  }

  Database *db = new Database();
  grn_ctx *ctx = &db->context;
  grn_ctx_init(ctx, 0);
  if (args[0]->IsUndefined()) {
    db->database = grn_db_create(ctx, NULL, NULL);
  } else if(args[0]->IsString()) {
    String::Utf8Value path(args[0]->ToString());
    GRN_DB_OPEN_OR_CREATE(ctx, *path, NULL, db->database);
  } else {
    return ThrowException(Exception::TypeError(String::New("Bad parameter")));
  }

  db->Wrap(args.Holder());
  return args.This();
}

void Database::CommandWork(uv_work_t* req) {
  Baton *baton = static_cast<Baton*>(req->data);
  int rc = -1;
  int flags;
  grn_ctx *ctx = &baton->context;

  grn_ctx_init(ctx, 0);
  grn_ctx_use(ctx, baton->database);
  rc = grn_ctx_send(ctx, baton->command, baton->command_length, 0);
  if (rc < 0) {
    baton->error = 1;
    return;
  }
  if (ctx->rc != GRN_SUCCESS) {
    baton->error = 2;
    return;
  }

  grn_ctx_recv(ctx, &baton->result, &baton->result_length, &flags);
  if (ctx->rc < 0) {
    baton->error = 3;
    return;
  }
  baton->error = 0;
  return;
}

void Database::CommandAfter(uv_work_t* req) {
  HandleScope scope;
  Baton* baton = static_cast<Baton*>(req->data);
  if (baton->error) {
      Local<Value> error = Exception::Error(String::New(baton->context.errbuf));
      Local<Value> argv[] = { error };
      baton->callback->Call(v8::Context::GetCurrent()->Global(), 1, argv);
  } else {
      Local<Value> argv[] = {
        Local<Value>::New(Undefined()),
        Local<Value>::New(String::New(baton->result, baton->result_length))
      };
      baton->callback->Call(v8::Context::GetCurrent()->Global(), 2, argv);
  }
  grn_ctx_fin(&baton->context);
  delete baton->command;
  baton->callback.Dispose();
  delete baton;
}

Handle<Value> Database::Command(const Arguments& args) {
  HandleScope scope;
  Database *db = ObjectWrap::Unwrap<Database>(args.Holder());
  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New("Bad parameter")));
  }

  bool callback_given = false;
  Local<Function> callback;
  if (args.Length() >= 2) {
    if (!args[1]->IsFunction()) {
      return ThrowException(Exception::TypeError(String::New("Second argument must be a callback function")));
    } else {
      callback = Local<Function>::Cast(args[1]);
      callback_given = true;
    }
  }

  Baton* baton = new Baton();
  baton->request.data = baton;
  baton->callback = Persistent<Function>::New(callback);

  String::Utf8Value command(args[0]->ToString());
  baton->database = db->database;

  baton->command = new char[command.length()];
  memcpy(baton->command, *command, command.length());
  baton->command_length = command.length();
  uv_queue_work(uv_default_loop(),
      &baton->request,
      CommandWork,
      CommandAfter
      );
  return Undefined();
}

Handle<Value> Database::CommandSync(const Arguments& args) {
  HandleScope scope;

  Database *db = ObjectWrap::Unwrap<Database>(args.Holder());
  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New("Bad parameter")));
  }

  int rc = -1;
  grn_ctx *ctx = &db->context;
  char *result;
  unsigned int result_length;
  int flags;
  String::Utf8Value command(args[0]->ToString());

  rc = grn_ctx_send(ctx, *command, command.length(), 0);
  if (rc < 0) {
    return ThrowException(Exception::Error(String::New("grn_ctx_send returned error")));
  }
  if (ctx->rc != GRN_SUCCESS) {
    return ThrowException(Exception::Error(String::New(ctx->errbuf)));
  }
  grn_ctx_recv(ctx, &result, &result_length, &flags);
  if (ctx->rc < 0) {
    return ThrowException(Exception::Error(String::New("grn_ctx_recv returned error")));
  }

  return scope.Close(String::New(result, result_length));
}

void InitGroonga(Handle<Object> target) {
  HandleScope scope;
  grn_init();

  Database::Initialize(target);
}

} // namespace node_groonga

NODE_MODULE(groonga, node_groonga::InitGroonga);
