At the moment it's not possisble to build svelteKit to single js file or in a flatten structure
https://github.com/sveltejs/kit/issues/3882

This means that it cannot be stored in SPIFFS as it only support upto 32 bytes filenames include filepath

For the time being the esp32 will serve at static html file
