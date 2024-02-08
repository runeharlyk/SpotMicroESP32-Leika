import { LoaderUtils } from "three";
import URDFLoader, { type URDFRobot } from "urdf-loader"
import { XacroLoader } from "xacro-parser"

export const loadModelAsync = async (url:string):Promise<[URDFRobot, string[]]> => {
  return new Promise((resolve, reject) => {
    const xacroLoader = new XacroLoader();
    
    xacroLoader.load(url, async (xml) => {
      const urdfLoader = new URDFLoader();
      
      urdfLoader.workingPath = LoaderUtils.extractUrlBase(url);
      
      try {
        const model = urdfLoader.parse(xml);
        model.rotation.x = -Math.PI / 2;
        model.rotation.z = Math.PI / 2;
        model.traverse(c => c.castShadow = true);
        model.updateMatrixWorld(true);
        model.scale.setScalar(10);
        const joints = Object.entries(model.joints)
            .filter(joint => joint[1]._jointType !== 'fixed')
            .map(joint => joint[0])

        resolve([model, joints]); 
      } catch (error) {
        reject(error); 
      }
    }, (error) => reject(error)); 
  });
}