
var server = require("./server");
var router = require("./router");
var requestHandlers = require("./requestHandlers");

var handle = {}
handle["/"] = requestHandlers.onSayHello;
handle["/SayHello"] = requestHandlers.onSayHello;
handle["/GenerateSkyBox"] = requestHandlers.onGenerateSkyBox;
handle["/QueryJob"] = requestHandlers.onQueryJob;

server.start(router.route, handle);