Temiloluwa Awosiyan - Plugin UI
In charge of handling the front end, enabling a comfortable workflow for designers to adjust camera functionality without using code.

Developed the gizmo UI using the Unreal Engine Editor Utility Widget, using the provided UI options to create a panel that would emit signals to a script to cause various different behaviors. These behaviors could be dynamically chosen depending on what element in the scene was pressed, allowing for customization of camera transition zone behaviors using the gizmo UI tools.

Noah Simmons - Core Functionality

In charge of handling the back end, developing various camera behaviors to enable a flexible system that empowers the current Unreal Engine 5 camera system.

Total contributions: 50/50

Group Formation

Group formation was chosen based on our shared GDW team, as we are the two programmers on our team. We decided to limit ourselves to 2 as we had previous work experience over the last few months, and we were the only two GDW programmers in the Game Engines course.

<img width="1195" height="254" alt="image" src="https://github.com/user-attachments/assets/96d08074-c38a-4a8a-9c13-5291f4976d89" />

The interactive media scenario that we will develop for the assignment is a camera plugin with a UI component enabling the development of an atmospheric walking simulator where the user is expected to reach the end of the environment for a concluding cutscene. The game contains various different environments that will cause the camera to move across different positions to highlight interactable elements.

Singleton I:

<img width="1134" height="731" alt="image" src="https://github.com/user-attachments/assets/2af29de7-22f3-4dc2-9a29-693a66738994" />

The CameraManager sets its singleton through the CameraSystemGameMode object in Unreal. Since static variables don't exist, we instead mimic a singleton by setting an instance variable in a type that is guaranteed to only have one instance in the level (The GameMode class)

Singleton II
<img width="698" height="712" alt="image" src="https://github.com/user-attachments/assets/2c6f1be6-686a-45d6-919c-5e73b5509cf8" />

The CameraWidget and CameraWidgetHelper both act as singletons, since the CameraWidget editor window runs on one instance and sets its reference to a CameraWidgetHelper that also acts as a singleton to anything that wants to access the CameraWidget and/or its Helper. For context, CameraWidgetHelper serves as a class that gives the CameraWidget access to some live references in the Unreal scene


Camera Manager Command Pattern

<img width="1500" height="1099" alt="image" src="https://github.com/user-attachments/assets/4a643297-14f8-4001-96cb-e43eb47189f8" />

At any given time, the CameraManager is responsible for calling the Process() function on Camera Components/Behaviours. These behaviours are functions that perform transformation logic and return the next position for the camera in that tick.
This is an implementation of the Command pattern because the Camera Manager is mainly responsible for calling the functions encapsulated in Components, which serve as commands in this case. These commands are what do the actual transformations within the camera, and the camera manager only sees them as two variables, variables which can be interchanged or even called in other contexts entirely as their own standalone logic. 

Widget UI Command Pattern

<img width="1189" height="958" alt="image" src="https://github.com/user-attachments/assets/8fcd26f3-186a-4fa5-b42a-b59a32f99f84" />

For the Widget UI, OnClicked functions are detected in CameraWidget, which executes any command derived from WidgetCommand. Commands such as ResetCommand, SplinePreviewCommand, and SwitchPageCommand in their Execute function, will then call the appropriate logic on CameraWidget to perform the operations they want to do.


Widget Based Factory Pattern

<img width="811" height="641" alt="image" src="https://github.com/user-attachments/assets/3e8cbd28-8c9a-4b3e-8bf3-8473eead2160" />

For every instantiation done through the widget, a factory pattern through the base class WidgetBasedSpawner is implemented. 

StaticCameraSpawner handles spawning a MoveToComponent at the exact location of your viewport camera so that users can easily set where they want a static camera to go in their composition. Will be expanded with a rotation behaviour to capture the editor's full camera state. This is called when pressing a dedicated button on the widget for this feature.

PlayerDummySpanwer spawns the dummy model for the player in editor camera spline previews. This is done on the ResetDummy button in the widget

Player Interaction VFX Factory pattern 
<img width="1354" height="556" alt="image" src="https://github.com/user-attachments/assets/58870b60-e1bc-4139-af11-804d94948ce1" />

Handles the visual effects of a player interacting with an interactable object in our game. Can be connected to the camera system by moving or affecting a camera behaviour through this event.

Plugin/DLL implementation:

This is packaged into a plugin recognized by Unreal Engine, and the purpose of this project is that the project itself is used as a tool/plugin rather than it be a game on its own. The release is a folder of the plugin we showed in the video, containing all the blueprints and classes for the camera system, with the idea that this system can be integrated with any new project.
