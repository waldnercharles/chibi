var Module = {
  arguments: ['/request.json'],
  print: function(message) { document.getElementById('result').textContent += '\n' + message; },
  printErr: function(message) { document.getElementById('result').textContent += '\nSTDERR: ' + message; },
  onExit: function(status) { document.getElementById('result').textContent += '\nEXIT ' + status; },
  onAbort: function(reason) { document.getElementById('result').textContent += '\nABORT ' + reason; }
};
