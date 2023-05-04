const char index_simple_html[] = R"=====(<!DOCTYPE html>
<html lang="en">
	<head>
		<meta charset="utf-8" />
		<link rel="icon" href="/favicon.png" />
		<meta name="viewport" content="width=device-width" />
		<meta http-equiv="content-security-policy" content="">
        <style>
			body {
				margin:0;
				padding:0;
				display:flex;
				justify-content: center;
				align-items: center;
				background-color: #00bbe3;
			}

			.stream-wrapper {
				position: absolute;
				top: 0;
				left: 0;
				height: 100vh;
				width: 100vw;
				display: flex;
				align-items: center;
				justify-content: center;
				z-index: -1;
			}

			#stream {
				object-fit: contain;
				height: 100%;
				width: 100%;
			}
		</style>
	</head>
	<body>
		<h2>Spot Micro Controller</h2>
		<div class="stream-wrapper">
			<img id="stream"/>
		</div>
		<script>
            const init = () => {

                let websocket = new WebSocket("ws://192.168.0.175:81")
                websocket.onopen = (event) => {
                    console.log(event);
                };
                websocket.onmessage = (event) => {
                    console.log(event.data);
                }

                document.getElementById("stream").src = "//192.168.0.175/mjpeg/1"
            }
            init()
		</script>
	</body>
</html>)=====";

size_t index_simple_html_len = sizeof(index_simple_html)-1;