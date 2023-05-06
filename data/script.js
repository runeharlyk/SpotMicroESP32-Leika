let websocket, scene, camera, rendered, cube;

function lerp (start, end, amt){
    return (1-amt)*start+amt*end
}

let floats = new Float32Array(12);
let x, y, z;
const init = () => {

    websocket = new WebSocket(`ws://leika.local/`) //${location.hostname}
    websocket.binaryType = "arraybuffer";
    websocket.onopen = (event) => {
        console.log(event);
    };
    websocket.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
            let data = new Uint8Array(event.data);
            floats = new Float32Array(data.buffer);

            var toDeg = (Math.PI/180);
            
            cube.rotation.x = lerp (x, floats[5]*toDeg, 0.5);
            cube.rotation.z = lerp (x, floats[6]*toDeg, 0.5);
            cube.rotation.y = lerp (x, floats[7]*toDeg, 0.5);

            x = floats[5]*toDeg;
            y = floats[6]*toDeg;
            z = floats[7]*toDeg;

            document.querySelector(".rssi").innerHTML = "RSSI: " + floats[0] + "db";
            document.querySelector(".mpu_temp").innerHTML = "Temp: :" + Math.floor(floats[1]) + "°";
            document.querySelector(".x").innerHTML = "X: " + x;
            document.querySelector(".y").innerHTML = "Y: " + y;
            document.querySelector(".z").innerHTML = "Z: " + z;
            document.querySelector(".temp").innerHTML = "Cpu temp: " + Math.floor(floats[8]) + "°";
            document.querySelector(".uss_r").innerHTML = "USS R: " + floats[9]+ "cm";
            document.querySelector(".uss_l").innerHTML = "USS L: " + floats[10] + "cm";
            document.querySelector(".heap").innerHTML = "Heap: " + humanFileSize(floats[11]);
            document.querySelector(".psram").innerHTML = "psram: " + humanFileSize(floats[12]);
            console.log(x,y,z);
            renderer.render(scene, camera);
        } else {
            console.log(event.data);
        }
    }

    document.getElementById("stream").src = `//leika.local/stream`
}

function parentWidth(elem) {
  return elem.parentElement.clientWidth;
}

function parentHeight(elem) {
  return elem.parentElement.clientHeight;
}

function init3D(){
  scene = new THREE.Scene();
  //scene.background = new THREE.Color(0xffffff00);

  camera = new THREE.PerspectiveCamera(75, parentWidth(document.getElementById("3Dcube")) / parentHeight(document.getElementById("3Dcube")), 0.1, 1000);

  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setClearColor( 0x000000, 0 ); 
  renderer.setSize(parentWidth(document.getElementById("3Dcube")), parentHeight(document.getElementById("3Dcube")));

  document.getElementById('3Dcube').appendChild(renderer.domElement);

  // Create a geometry
  const geometry = new THREE.BoxGeometry(2, 1, 6);

  // Materials of each face
  var cubeMaterials = [
    new THREE.MeshBasicMaterial({color:0x03045e}),
    new THREE.MeshBasicMaterial({color:0x023e8a}),
    new THREE.MeshBasicMaterial({color:0x0077b6}),
    new THREE.MeshBasicMaterial({color:0x03045e}),
    new THREE.MeshBasicMaterial({color:0x023e8a}),
    new THREE.MeshBasicMaterial({color:0x0077b6}),
  ];

  const material = new THREE.MeshFaceMaterial(cubeMaterials);

  cube = new THREE.Mesh(geometry, material);
  scene.add(cube);
  camera.position.z = 5;
  renderer.render(scene, camera);
}

// Resize the 3D object when the browser window changes size
function onWindowResize(){
  camera.aspect = parentWidth(document.getElementById("3Dcube")) / parentHeight(document.getElementById("3Dcube"));
  //camera.aspect = window.innerWidth /  window.innerHeight;
  camera.updateProjectionMatrix();
  //renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setSize(parentWidth(document.getElementById("3Dcube")), parentHeight(document.getElementById("3Dcube")));

}

window.addEventListener('resize', onWindowResize, false);

// Create the 3D representation
init3D();
init();


function humanFileSize(bytes, si=false, dp=1) {
    const thresh = si ? 1000 : 1024;
  
    if (Math.abs(bytes) < thresh) {
      return bytes + ' B';
    }
  
    const units = si 
      ? ['kB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'] 
      : ['KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB', 'ZiB', 'YiB'];
    let u = -1;
    const r = 10**dp;
  
    do {
      bytes /= thresh;
      ++u;
    } while (Math.round(Math.abs(bytes) * r) / r >= thresh && u < units.length - 1);
  
  
    return bytes.toFixed(dp) + ' ' + units[u];
  }