#include "vk_rendering.h"

namespace vkutil {
    void cmdSetViewportAndScissor(VkCommandBuffer cmdBuffer, uint32_t width, uint32_t height) {
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, { width, height } };
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    }

    VkRenderingAttachmentInfo renderingAttachmentInfo(VkImageView imageView, VkImageLayout imageLayout, VkClearValue clearValue, 
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp) {
        VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        attachmentInfo.imageView = imageView;
        attachmentInfo.imageLayout = imageLayout;
        attachmentInfo.loadOp = loadOp;
        attachmentInfo.storeOp = storeOp;
        attachmentInfo.clearValue = clearValue;
        return attachmentInfo;
    }

    VkRenderingAttachmentInfo renderingdepthAttachmentInfo(VkImageView imageView, VkImageLayout imageLayout, float clearValue,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp) {
        VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        attachmentInfo.imageView = imageView;
        attachmentInfo.imageLayout = imageLayout;
        attachmentInfo.loadOp = loadOp;
        attachmentInfo.storeOp = storeOp;
        attachmentInfo.clearValue.depthStencil = {clearValue, 0};
        return attachmentInfo;
    }

    void cmdBeginColorDepthRendering(VkCommandBuffer cmd, VkExtent2D extent, const VkRenderingAttachmentInfo& colorAttachment, const VkRenderingAttachmentInfo& depthAttachment) {
        VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        renderingInfo.renderArea = { 0, 0, extent };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;
        renderingInfo.pStencilAttachment = nullptr;
        vkCmdBeginRendering(cmd, &renderingInfo);
    }

    void cmdBeginDepthOnlyRendering(VkCommandBuffer cmd, VkExtent2D extent, const VkRenderingAttachmentInfo& depthAttachment) {
        VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.pColorAttachments = nullptr;
        renderingInfo.pDepthAttachment = &depthAttachment;
        renderingInfo.pStencilAttachment = nullptr;
        vkCmdBeginRendering(cmd, &renderingInfo);
    }

    void cmdBeginColorOnlyRendering(VkCommandBuffer cmd, VkExtent2D extent, const VkRenderingAttachmentInfo& colorAttachment) {
        VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = nullptr;
        renderingInfo.pStencilAttachment = nullptr;
        vkCmdBeginRendering(cmd, &renderingInfo);
    }

    void cmdEndRendering(VkCommandBuffer cmd) {
        vkCmdEndRendering(cmd);
    }
}