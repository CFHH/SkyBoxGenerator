var http = require("http");
var url = require("url");

function start(route, handle)
{
    function onRequest(request, response)
    {
        var urlObj = url.parse(request.url);
        var pathname = urlObj.pathname;
        var query = urlObj.query;
        console.log("Request for " + pathname + " received." + " query: " + query);
        route(pathname, query, handle, response);
    }
    var port = 8888;
    http.createServer(onRequest).listen(port);
    console.log("Server has started on " + port + ".");
}

exports.start = start;
