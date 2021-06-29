var PROTO_PATH = __dirname + '/../Source/SkyBoxGenerator/protos/skybox.proto';
var SERVER_ADDRESS = 'localhost:50051';

var querystring = require("querystring");
var parseArgs = require('minimist');
var grpc = require('@grpc/grpc-js');
var protoLoader = require('@grpc/proto-loader');
var packageDefinition = protoLoader.loadSync(
    PROTO_PATH,
    {keepCase: true,
     longs: String,
     enums: String,
     defaults: true,
     oneofs: true
    });
var skybox_proto = grpc.loadPackageDefinition(packageDefinition).skybox;
var rpcClient = new skybox_proto.SkyBoxService(SERVER_ADDRESS, grpc.credentials.createInsecure());


function respondHttpRequest(response, text) {
    response.writeHead(200, { "Content-Type": "text/plain" });
    response.write(text);
    response.end();
}

function onSayHello(query, response) {
    var queryObj = querystring.parse(query);

	var err = null;
	if (!("name" in queryObj)) {
	    err = "name is missing";
	}
	if (err != null) {
	    var obj = { error: err };
	    var responseText = JSON.stringify(obj);
	    respondHttpRequest(response, responseText);
	    return;
	}

	function onRpcReply(rpcError, rpcReply) {
	    var responseText = "error";
	    if (rpcError) {
	        responseText = rpcError;
	    } else {
	        console.log('RPC Reply:', rpcReply.message);
	        responseText = JSON.stringify(rpcReply);
	    }
	    respondHttpRequest(response, responseText);
	}

	//console.log('rpcClient.sayHello', rpcClient.sayHello);
	var rpcRequest = {}
	rpcRequest.name = queryObj.name;
	rpcClient.sayHello(rpcRequest, onRpcReply);
}

function onGenerateSkyBox(query, response)
{
    var queryObj = querystring.parse(query);

    var err = null;
    if (!("x" in queryObj)) {
        err = "x is missing";
    } else if(!("y" in queryObj)) {
        err = "y is missing";
    } else if (!("z" in queryObj)) {
        err = "z is missing";
    }
    if (err != null) {
        var obj = { error: err };
        var responseText = JSON.stringify(obj);
        respondHttpRequest(response, responseText);
        return;
    }

    function onRpcReply(rpcError, rpcReply) {
        var responseText = "error";
        if (rpcError) {
            responseText = rpcError;
        } else {
            responseText = JSON.stringify(rpcReply);
        }
        respondHttpRequest(response, responseText);
    }

    var rpcRequest = {}
    rpcRequest.position = {}
    rpcRequest.position.x = queryObj.x;
    rpcRequest.position.y = queryObj.y;
    rpcRequest.position.z = queryObj.z;
    rpcClient.generateSkyBox(rpcRequest, onRpcReply);
}

function onQueryJob(query, response)
{
    var queryObj = querystring.parse(query);

    var err = null;
    if (!("job_id" in queryObj)) {
        err = "job_id is missing";
    }
    if (err != null) {
        var obj = { error: err };
        var responseText = JSON.stringify(obj);
        respondHttpRequest(response, responseText);
        return;
    }

    function onRpcReply(rpcError, rpcReply) {
        var responseText = "error";
        if (rpcError) {
            responseText = rpcError;
        } else {
            responseText = JSON.stringify(rpcReply);
        }
        respondHttpRequest(response, responseText);
    }

    var rpcRequest = {}
    rpcRequest.job_id = queryObj.job_id;
    rpcClient.queryJob(rpcRequest, onRpcReply);
}

exports.onSayHello = onSayHello;
exports.onGenerateSkyBox = onGenerateSkyBox;
exports.onQueryJob = onQueryJob;