var Module = {
  arguments: ['--reuse', '/request.json', '/cache'],
  print: function(message) { document.getElementById('result').textContent += '\n' + message; },
  printErr: function(message) { document.getElementById('result').textContent += '\nSTDERR: ' + message; },
  onExit: function(status) { document.getElementById('result').textContent += '\nEXIT ' + status; },
  onAbort: function(reason) { document.getElementById('result').textContent += '\nABORT ' + reason; }
};
