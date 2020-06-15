function updateBack(enable) {
    if (enable)
		document.getElementById("back").classList.remove("disabled");
	else
		document.getElementById("back").classList.add("disabled");
}

function updateForward(enable) {
    if (enable)
		document.getElementById("forward").classList.remove("disabled");
	else
		document.getElementById("forward").classList.add("disabled");
}

function updateLoading(is_loading) {
    if (is_loading) {
		document.getElementById("refresh").style.display = "none";
		document.getElementById("stop").style.display = "inline-block";
	} else {
		document.getElementById("refresh").style.display = "inline-block";
		document.getElementById("stop").style.display = "none";
	}
}

function updateURL(url) {
	document.getElementById('address').value = url;
}

function key_response(e) {
  var key = e.which;
  // need first check if key < 58 then just do OnClickURL(key) and return
  switch (key) {
    case 72:
      OnBack(e, null);
      break;
    case 76:
      OnForward(e, null);
      break;
    case 49:
      OnClickURL(key);
      break;
    case 50:
      OnClickURL(key);
      break;
    case 51:
      OnClickURL(key);
      break;
    case 52:
      OnClickURL(key);
      break;
    case 81:
      OnQuit(e, null);
      break;
    case 37:
      OnScrollLeft(e, null);
      break;
    case 38:
      OnScrollUp(e, null);
      break;
    case 39:
      OnScrollRight(e, null);
      break;
    case 40:
      OnScrollDown(e, null);
      break;
    case 83:
      OnNumberLinks(e, null);
      break;
  }    
}
