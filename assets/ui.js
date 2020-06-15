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
  if (key > 48 && key < 58) {OnClickURL(key); return;}
  switch (key) {
    case 72: //H
      OnBack();
      break;
    case 76: //L
      OnForward();
      break;
    case 81: //q
      OnQuit();
      break;
    case 37: //left arrow
      OnScrollLeft();
      break;
    case 38: //up arrow
      OnScrollUp();
      break;
    case 39: //right arrow
      OnScrollRight();
      break;
    case 40: //down arrow
      OnScrollDown();
      break;
    case 83: //s[how]
      OnNumberLinks();
      break;
  }    
}
