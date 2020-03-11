#include "facenet_tf.h"
#include <dirent.h>
#include <cstring>

template<>
FacenetClassifier<>::FacenetClassifier(string operation, string model_path) {
    this->operation = operation;
    this->model_path = model_path;
    ReadBinaryProto(tensorflow::Env::Default(), model_path.c_str(), &graph_def);
    tensorflow::SessionOptions options;
    tensorflow::NewSession(options, &session);
    session->Create(graph_def);
}

template<>
void FacenetClassifier<>::save_labels(const std::string &file) {
    labels_file.open(file, fstream::out);

    for (Label label: class_labels) {
        labels_file << label.class_number << " " << label.class_name << endl;
    }
    labels_file.close();
}

template<>
void FacenetClassifier<>::load_labels(const std::string &file) {
    class_labels.clear();
    labels_file.open(file, fstream::in);
    int count = 0;
    while (true) {
        if (labels_file.eof())
            break;
        Label label;
        labels_file >> label.class_number >> label.class_name;
        class_labels.push_back(label);
        count++;
    }
    labels_file.close();
}

template<>
void FacenetClassifier<>::preprocess_input_mat(cv::Mat &image) {
    //mean and std
    cv::Mat temp = image.reshape(1, image.rows * 3);
    cv::Mat mean3;
    cv::Mat stddev3;
    cv::meanStdDev(temp, mean3, stddev3);

    double mean_pxl = mean3.at<double>(0);
    double stddev_pxl = stddev3.at<double>(0);
    cv::Mat image2;
    image.convertTo(image2, CV_64FC1);
    image = image2;
    image = image - cv::Scalar(mean_pxl, mean_pxl, mean_pxl);
    image = image / stddev_pxl;
}

template<>
std::pair<std::vector<std::string>, std::vector<int>>
FacenetClassifier<>::parse_images_path(string directory_path, int depth) {
    std::pair<std::vector<std::string>, std::vector<int>> files;
    DIR *dir;
    struct dirent *entry;
    static int class_count = -1;
    string class_name;
    string file_name, file_path;
    if ((dir = opendir(directory_path.c_str())) != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                class_name = string(entry->d_name);
                class_count++;
                auto r = parse_images_path(directory_path + "/" + class_name, depth + 1);
                files.first.insert(files.first.end(), r.first.begin(), r.first.end());
                files.second.insert(files.second.end(), r.second.begin(), r.second.end());
                Label label;
                label.class_number = class_count;
                label.class_name = class_name;
                class_labels.push_back(label);
            } else if (entry->d_type != DT_DIR) {
                file_name = string(entry->d_name);
                file_path = directory_path + "/" + file_name;
                files.first.emplace_back(file_path);
                files.second.emplace_back(class_count);
            }
        }
        closedir(dir);
    }
    return files;
}

template<>
Tensor FacenetClassifier<>::create_input_tensor(const cv::Mat &image) {
    Tensor input_tensor(DT_FLOAT, TensorShape({1, 160, 160, 3}));
    // get pointer to memory for that Tensor
    float *p = input_tensor.flat<float>().data();
    // create a "fake" cv::Mat from it
    Mat camera_image(160, 160, CV_32FC3, p);
    image.convertTo(camera_image, CV_32FC3);
    return input_tensor;
}

template<>
Tensor FacenetClassifier<>::create_phase_tensor() {
    tensorflow::Tensor phase_tensor(tensorflow::DT_BOOL, tensorflow::TensorShape());
    phase_tensor.scalar<bool>()() = false;
    return phase_tensor;
}

template<>
cv::Mat FacenetClassifier<>::run(Tensor &input_tensor, Tensor &phase_tensor) {
    string input_layer = "input:0";
    string phase_train_layer = "phase_train:0";
    string output_layer = "embeddings:0";
    std::vector<tensorflow::Tensor> outputs;
    std::vector<std::pair<string, tensorflow::Tensor>> feed_dict = {
            {input_layer,       input_tensor},
            {phase_train_layer, phase_tensor},
    };
    tensorflow::SessionOptions options;
    tensorflow::NewSession(options, &session);
    session->Create(graph_def);
    Status run_status = session->Run(feed_dict, {output_layer}, {}, &outputs);
    if (!run_status.ok()) {
        LOG(ERROR) << "Running model failed: " << run_status << "\n";
        return cv::Mat();
    }

    float *p = outputs[0].flat<float>().data();
    Mat mat_row(cv::Size(128, 1), CV_32F, p, Mat::AUTO_STEP);
    session->Close();
    delete session;
    return mat_row;
}

template<>
void FacenetClassifier<>::train(const cv::Mat &samples, const std::vector<int> &labels) {
    classifier.train(samples, labels);
}
