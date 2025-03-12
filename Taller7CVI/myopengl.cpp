#include "myopengl.hpp"

namespace myopengl {

	glm::mat4 myopengl::scale(const glm::vec3& scaleVector)
	{
		glm::mat4 model = glm::mat4(1.0f);
		return glm::scale(model, scaleVector);
	}




}