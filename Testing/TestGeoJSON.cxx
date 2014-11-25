//VTK includes
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderWindow.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataWriter.h>
#include <vtksys/CommandLineArguments.hxx>

//Reader includes
#include "vtkGeoJSONReader.h"

#include <fstream>
#include <iostream>
#include <string>

bool SetupFeatureProperties(Json::Value& schemaNode, vtkGeoJSONReader *reader);
int TestGeoJSON(int argc, char **argv);

//----------------------------------------------------------------------------
int main(int argc, char **argv)
{
   return TestGeoJSON(argc, argv);
}

//----------------------------------------------------------------------------
int TestGeoJSON(int argc, char **argv)
{
  // Setup command line arguments
  bool showHelp = false;
  std::string inputGeoJSONFile;
  bool stringInputMode = false;
  bool outlinePolygonsMode = false;
  bool triangulatePolygonsMode = false;
  std::string inputSchemaFile;
  std::string outputVtkFile;

  vtksys::CommandLineArguments arg;
  arg.Initialize(argc, argv);
  arg.StoreUnusedArguments(true);
  arg.AddArgument("-h", vtksys::CommandLineArguments::NO_ARGUMENT,
                  &showHelp, "show help message");
  arg.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT,
                  &showHelp, "show help message");
  arg.AddArgument("-j", vtksys::CommandLineArguments::SPACE_ARGUMENT,
                  &inputSchemaFile, "json-schema file specifying feature properties");
  arg.AddArgument("-o", vtksys::CommandLineArguments::NO_ARGUMENT,
                  &outlinePolygonsMode, "flag to set OutlinePolygons mode");
  arg.AddArgument("-s", vtksys::CommandLineArguments::NO_ARGUMENT,
                  &stringInputMode, "flag to load file as string before parsing");
  arg.AddArgument("-t", vtksys::CommandLineArguments::NO_ARGUMENT,
                  &triangulatePolygonsMode, "flag to set TriangulatePolygons mode");
  arg.AddArgument("-v", vtksys::CommandLineArguments::SPACE_ARGUMENT,
                  &outputVtkFile, "VTK output file");

  if (argc < 2 || !arg.Parse() || showHelp)
    {
    std::cerr << "\n"
              << "Test vtkGeoJSONReader" << "\n"
              << "Usage:  TestGeoJSON  input-geojson-file  [options]" << "\n"
              << "Options:" << "\n"
              << arg.GetHelp()
              << std::endl;
    return -1;
    }

  inputGeoJSONFile = argv[1];

  // Instantiate the GeoJSON reader
  vtkNew<vtkGeoJSONReader> reader;
  if (stringInputMode)
    {
    std::cout << "Setting StringInputMode true" << std::endl;
    reader->StringInputModeOn();
    // Load source file
    std::ifstream in(inputGeoJSONFile.c_str(), std::ios::in | std::ios::binary);
    if (in)
      {
      std::string content(std::istreambuf_iterator<char>(in.rdbuf()),
                          std::istreambuf_iterator<char>());
      in.close();
      reader->SetStringInput(content.c_str());
      }
    else
      {
      std::cerr << "Unable to open file " << argv[1] << std::endl;
      return -1;
      }
    }
  else
    {
    // Set source file
    reader->SetFileName(inputGeoJSONFile.c_str());
    }

  // Set triangulate and outline modes
  reader->SetOutlinePolygons(outlinePolygonsMode);
  reader->SetTriangulatePolygons(triangulatePolygonsMode);

  // Process schema
  if (!inputSchemaFile.empty())
    {
    // Open the file
    ifstream file;
    file.open(inputSchemaFile.c_str());

    if (!file.is_open())
      {
      std::cerr << "Unable to open file " << inputSchemaFile.c_str()
      << std::endl;
      return VTK_ERROR;
      }

    // Parse the file contents into Json::Value
    Json::Value schemaRoot;
    Json::Reader schemaReader;
    bool parsedSuccess = schemaReader.parse(file, schemaRoot, false);
    file.close();
    if (!parsedSuccess)
      {
      std::cerr << "Failed to parse JSON\n"
                << schemaReader.getFormatedErrorMessages() << std::endl;
      return VTK_ERROR;
      }
    if (!SetupFeatureProperties(schemaRoot, reader.GetPointer()))
      {
      std::cerr << "Problem parsing schema input file" << std::endl;
      return VTK_ERROR;
      }
    }


  // Read the input
  reader->Update();
  vtkPolyData *outputData = reader->GetOutput();

  // Write vtk file output
  if (!outputVtkFile.empty())
    {
    std::cout << "Writing poly data to " << outputVtkFile << std::endl;
    vtkNew<vtkPolyDataWriter> writer;
    writer->SetFileName(outputVtkFile.c_str());
    writer->SetInputData(outputData);
    writer->Update();
    }

  // Visualise in a render window
  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputData(outputData);

  vtkNew<vtkActor> actor;
  actor->SetMapper(mapper.GetPointer());

  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkRenderWindow> renderWindow;
  renderWindow->AddRenderer(renderer.GetPointer());

  vtkNew<vtkRenderWindowInteractor> renderWindowInteractor;
  renderWindowInteractor->SetRenderWindow(renderWindow.GetPointer());
  renderer->AddActor(actor.GetPointer());
  renderWindow->Render();
  renderWindowInteractor->Start();

  return EXIT_SUCCESS;
}

//----------------------------------------------------------------------------
bool SetupFeatureProperties(Json::Value& schemaNode, vtkGeoJSONReader *reader)
{
  if (schemaNode.isNull())
    {
    return false;
    }
  if (!schemaNode.isObject())
    {
    std::cerr << "properties-schema node is not an object" << std::endl;;
    return false;
    }

  // Get properties node
  Json::Value propertiesNode = schemaNode["properties"];
  if (!propertiesNode.isObject())
    {
    std::cerr << "schema properties node either missing or wrong json type"
              << std::endl;;
    return false;
    }

  Json::Value::Members propertyNames = propertiesNode.getMemberNames();
  Json::Value::Members::const_iterator nameIter = propertyNames.begin();
  for (; nameIter != propertyNames.end(); nameIter++)
    {
    std::string name = *nameIter;
    //std::cout << "Property name " << name << std::endl;

    Json::Value propertyNode = propertiesNode[name];
    Json::Value typeNode = propertyNode["type"];
    if (typeNode.isNull() || !typeNode.isString())
      {
      std::cerr << "Invalid schema for property " << name
                << " -- no \"type\" specified" << std::endl;;
      continue;
      }
    vtkIdType vtkDataType = VTK_VOID;
    std::string typeString = typeNode.asString();
    if ("boolean" == typeString)
      {
      vtkDataType = VTK_BIT;
      }
    else if ("integer" == typeString)
      {
      vtkDataType = VTK_INT;
      }
    else if ("number" == typeString)
      {
      vtkDataType = VTK_DOUBLE;
      }
    else if ("string" == typeString)
      {
      vtkDataType = VTK_STRING;
      }

    if (VTK_VOID == vtkDataType)
      {
      std::cerr << "Unspported data type " << typeString.c_str()
                << " for property " << name.c_str() << std::endl;
      }

    vtkVariant defaultValue;
    Json::Value defaultNode = propertyNode["default"];
    switch (defaultNode.type())
      {
      case Json::nullValue:
        std::cerr << "no default value specified for property "
                  << name.c_str() << std::endl;
        break;

      case Json::intValue:
      case Json::uintValue:
        defaultValue = vtkVariant(defaultNode.asInt());
        break;

      case Json::realValue:
        defaultValue = vtkVariant(defaultNode.asDouble());
        break;

      case Json::stringValue:
        defaultValue = vtkVariant(defaultNode.asString());
        break;

      default:
        std::cerr << "Unsupported default node type: "
                  << defaultNode.type() << std::endl;
        break;
      }

    std::size_t length = name.size();
    char charName[length+1];
    name.copy(charName, length);
    charName[length] = '\0';
    reader->AddFeatureProperty(charName, defaultValue);
    }

  return true;
}
