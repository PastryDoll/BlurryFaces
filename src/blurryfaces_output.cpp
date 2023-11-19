int CreateProject()
{
    // Declare a file pointer
    FILE *file;

    file = fopen("../saves/project.bf", "wb");

    // Check if the file was opened successfully
    if (file == NULL) {
        printf("Error opening the file!\n");
        return 1; // Return an error code
    }

    // Write content to the file
    fprintf(file, "Hello, this is a sample file created using C programming.\n");

    // Close the file
    fclose(file);

    printf("File created successfully!\n");

    return 0;

}
int MapProjectToArray()
{

}